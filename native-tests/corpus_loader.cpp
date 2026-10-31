#include "corpus_loader.h"

#include "sha256.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace d5corpus {
namespace {

// --- minimal flat-JSON field extraction ---------------------------------------
// Manifest lines are single-level objects: "key": "string" | "key": int |
// "key": null. Strings may contain \" and \\ escapes. Anything nested is
// out of schema and reported as malformed.

const char* skipWs(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

// Finds "key" : and returns a pointer just past the colon, or nullptr.
const char* fieldPos(const std::string& line, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    std::size_t pos = 0;
    while ((pos = line.find(pat, pos)) != std::string::npos) {
        // Key must not be embedded inside a longer identifier.
        const bool leftOk = pos == 0 || !(std::isalnum(line[pos - 1]) || line[pos - 1] == '_');
        const std::size_t after = pos + pat.size();
        const bool rightOk =
            after >= line.size() ||
            !(std::isalnum(line[after]) || line[after] == '_' || line[after] == '"');
        if (leftOk && rightOk) {
            const char* colon = skipWs(line.c_str() + after);
            if (*colon == ':') return skipWs(colon + 1);
        }
        pos = after;
    }
    return nullptr;
}

bool jsonStr(const std::string& line, const std::string& key, std::string* out) {
    const char* p = fieldPos(line, key);
    if (!p || *p != '"') return false;
    ++p;
    std::string v;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            v += p[1];
            p += 2;
        } else {
            v += *p++;
        }
    }
    if (*p != '"') return false;
    *out = v;
    return true;
}

bool jsonInt(const std::string& line, const std::string& key, int* out) {
    const char* p = fieldPos(line, key);
    if (!p || (!std::isdigit(*p) && *p != '-')) return false;
    char* end = nullptr;
    const long v = std::strtol(p, &end, 10);
    if (end == p) return false;
    *out = static_cast<int>(v);
    return true;
}

// A field may be an explicit JSON null (manifest "annotation").
bool jsonNull(const std::string& line, const std::string& key) {
    const char* p = fieldPos(line, key);
    return p && std::strncmp(p, "null", 4) == 0;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(s[a])) ++a;
    while (b > a && std::isspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::uint32_t rd32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t rd16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}

}  // namespace

bool resolveCorpusPath(const std::string& corpusDir, const std::string& rel,
                       std::string* out) {
    if (rel.empty()) return false;
    if (rel[0] == '/' || rel[0] == '\\' ||
        (rel.size() > 1 && rel[1] == ':') ||                    // C:\...
        rel.find("..") != std::string::npos) {
        return false;
    }
    *out = corpusDir + "/" + rel;
    return true;
}

bool loadManifest(const std::string& corpusDir, std::vector<ClipMeta>* out) {
    out->clear();
    const std::string path = corpusDir + "/manifest.jsonl";
    std::ifstream in(path);
    if (!in) {
        std::printf("  manifest: %s not found\n", path.c_str());
        return true;  // absent corpus is the documented SKIP path
    }
    std::string line;
    int lineno = 0;
    bool ok = true;
    while (std::getline(in, line)) {
        ++lineno;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        ClipMeta m;
        bool good = jsonStr(line, "clip_id", &m.clipId) &&
                    jsonStr(line, "file", &m.file) &&
                    jsonStr(line, "sha256", &m.sha256) &&
                    jsonInt(line, "fs", &m.fs) &&
                    jsonStr(line, "singer", &m.singer) &&
                    jsonStr(line, "context", &m.context) &&
                    jsonStr(line, "technique", &m.technique) &&
                    jsonStr(line, "vowel", &m.vowel) &&
                    jsonStr(line, "source", &m.source);
        if (good) {
            if (jsonNull(line, "annotation")) {
                m.annotation.clear();
            } else if (!jsonStr(line, "annotation", &m.annotation)) {
                good = false;
            }
        }
        if (good && m.fs != 44100 && m.fs != 48000) {
            std::printf("  manifest:%d: fs %d not in {44100, 48000}\n", lineno, m.fs);
            good = false;
        }
        if (good && m.sha256.size() != 64) {
            std::printf("  manifest:%d: sha256 must be 64 hex chars\n", lineno);
            good = false;
        }
        std::string resolved;
        if (good && !resolveCorpusPath(corpusDir, m.file, &resolved)) {
            std::printf("  manifest:%d: file path %s escapes the corpus dir\n",
                        lineno, m.file.c_str());
            good = false;
        }
        if (good && !m.annotation.empty() &&
            !resolveCorpusPath(corpusDir, m.annotation, &resolved)) {
            std::printf("  manifest:%d: annotation path %s escapes the corpus dir\n",
                        lineno, m.annotation.c_str());
            good = false;
        }
        if (!good) {
            std::printf("  manifest:%d: MALFORMED line: %s\n", lineno, line.c_str());
            ok = false;
            continue;
        }
        out->push_back(m);
    }
    return ok;
}

bool readWav16(const std::string& path, std::vector<float>* samples, int* fs,
               std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *err = "cannot open " + path;
        return false;
    }
    const std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) != 0 ||
        std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
        *err = path + ": not a RIFF/WAVE file";
        return false;
    }
    std::uint16_t fmt = 0, ch = 0, bits = 0;
    std::uint32_t rate = 0;
    const std::uint8_t* pcm = nullptr;
    std::size_t pcmBytes = 0;
    std::size_t pos = 12;
    while (pos + 8 <= d.size()) {
        const std::uint32_t sz = rd32(d.data() + pos + 4);
        const std::size_t body = pos + 8;
        if (body + sz > d.size()) break;
        if (std::memcmp(d.data() + pos, "fmt ", 4) == 0 && sz >= 16) {
            fmt = rd16(d.data() + body);
            ch = rd16(d.data() + body + 2);
            rate = rd32(d.data() + body + 4);
            bits = rd16(d.data() + body + 14);
        } else if (std::memcmp(d.data() + pos, "data", 4) == 0) {
            pcm = d.data() + body;
            pcmBytes = sz;
        }
        pos = body + sz + (sz & 1);  // chunks are 2-byte aligned
    }
    if (fmt != 1 || ch != 1 || bits != 16) {
        *err = path + ": schema requires 16-bit PCM mono (fmt=" +
               std::to_string(fmt) + " ch=" + std::to_string(ch) +
               " bits=" + std::to_string(bits) + ")";
        return false;
    }
    if (rate != 44100 && rate != 48000) {
        *err = path + ": sample rate " + std::to_string(rate) +
               " not in {44100, 48000}";
        return false;
    }
    if (!pcm || pcmBytes < 2) {
        *err = path + ": no data chunk";
        return false;
    }
    const std::size_t n = pcmBytes / 2;
    if (static_cast<double>(n) / rate > 15.0) {
        *err = path + ": duration " + std::to_string(static_cast<double>(n) / rate) +
               " s exceeds the 15 s schema bound";
        return false;
    }
    samples->resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::int16_t v = static_cast<std::int16_t>(
            pcm[2 * i] | (static_cast<unsigned>(pcm[2 * i + 1]) << 8));
        (*samples)[i] = static_cast<float>(v) / 32768.0f;
    }
    *fs = static_cast<int>(rate);
    return true;
}

bool writeWav16(const std::string& path, const std::vector<float>& samples, int fs,
                std::string* err) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        *err = "cannot write " + path;
        return false;
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size() * 2);
    const std::uint32_t riff = 36 + dataBytes;
    auto w32 = [&](std::uint32_t v) {
        const char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                           static_cast<char>((v >> 16) & 0xFF),
                           static_cast<char>((v >> 24) & 0xFF)};
        out.write(b, 4);
    };
    auto w16 = [&](std::uint16_t v) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
        out.write(b, 2);
    };
    out.write("RIFF", 4);
    w32(riff);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    w32(16);
    w16(1);            // PCM
    w16(1);            // mono
    w32(static_cast<std::uint32_t>(fs));
    w32(static_cast<std::uint32_t>(fs * 2));  // byte rate
    w16(2);            // block align
    w16(16);           // bits
    out.write("data", 4);
    w32(dataBytes);
    for (const float x : samples) {
        const float c = std::max(-1.0f, std::min(1.0f, x));
        w16(static_cast<std::uint16_t>(static_cast<std::int16_t>(
            std::lrint(static_cast<double>(c) * 32767.0))));
    }
    if (!out) {
        *err = "write failed " + path;
        return false;
    }
    return true;
}

bool loadAnnotation(const std::string& path, std::vector<NoteSeg>* out,
                    std::string* err) {
    std::ifstream in(path);
    if (!in) {
        *err = "cannot open " + path;
        return false;
    }
    out->clear();
    std::string line;
    int lineno = 0;
    double prevEnd = -1.0;
    while (std::getline(in, line)) {
        ++lineno;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (lineno == 1 && line.find("t_start") != std::string::npos) continue;
        std::istringstream ss(line);
        std::string f0, f1, f2, f3, extra;
        if (!std::getline(ss, f0, ',') || !std::getline(ss, f1, ',') ||
            !std::getline(ss, f2, ',') || !std::getline(ss, f3, ',') ||
            std::getline(ss, extra, ',')) {
            *err = path + ":" + std::to_string(lineno) + ": expected 4 CSV fields";
            return false;
        }
        NoteSeg s;
        try {
            s.tStart = std::stod(trim(f0));
            s.tEnd = std::stod(trim(f1));
            s.f0Hz = std::stod(trim(f2));
            s.midi = std::stoi(trim(f3));
        } catch (...) {
            *err = path + ":" + std::to_string(lineno) + ": unparseable row";
            return false;
        }
        if (s.tEnd <= s.tStart || s.f0Hz <= 0.0 || s.tStart < 0.0) {
            *err = path + ":" + std::to_string(lineno) +
                   ": bad segment bounds/f0";
            return false;
        }
        if (s.tStart < prevEnd - 1e-6) {
            *err = path + ":" + std::to_string(lineno) +
                   ": segments must be time-ordered, non-overlapping";
            return false;
        }
        prevEnd = s.tEnd;
        out->push_back(s);
    }
    if (out->empty()) {
        *err = path + ": no segments";
        return false;
    }
    return true;
}

bool loadClip(const std::string& corpusDir, const ClipMeta& meta, Clip* out,
              std::string* err) {
    std::string wavPath;
    if (!resolveCorpusPath(corpusDir, meta.file, &wavPath)) {
        *err = "file path escapes corpus dir";
        return false;
    }
    const std::string actual = d5sha::fileSha256(wavPath);
    if (actual.empty()) {
        *err = "cannot read " + wavPath;
        return false;
    }
    if (actual != meta.sha256) {
        *err = "SHA-256 mismatch for " + meta.file + " (manifest " +
               meta.sha256.substr(0, 16) + "..., actual " + actual.substr(0, 16) + "...)";
        return false;
    }
    int fs = 0;
    if (!readWav16(wavPath, &out->samples, &fs, err)) return false;
    if (fs != meta.fs) {
        *err = "manifest fs " + std::to_string(meta.fs) + " != WAV fs " +
               std::to_string(fs);
        return false;
    }
    out->fs = fs;
    if (!meta.annotation.empty()) {
        std::string annPath;
        if (!resolveCorpusPath(corpusDir, meta.annotation, &annPath) ||
            !loadAnnotation(annPath, &out->notes, err))
            return false;
    }
    out->meta = meta;
    return true;
}

}  // namespace d5corpus
