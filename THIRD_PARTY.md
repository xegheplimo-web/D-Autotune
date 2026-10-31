# THIRD_PARTY.md — third-party materials used by this repository

## VocalSet (singing voice dataset)
- **Used for**: real-vocal validation corpus (`docs/corpus/clips/*`), subset of 20 clips
  (4 singers: f1, f3, m1, m3; technique `straight`; contexts `long_tones` + `scales`).
- **Source**: Julia Wilkins et al., "VocalSet: A Singing Voice Dataset", Zenodo,
  https://doi.org/10.5281/zenodo.1193957 (also available as 1442513). Upstream project
  https://github.com/ausacld/vocalset .
- **License**: Creative Commons Attribution 4.0 International (CC BY 4.0),
  https://creativecommons.org/licenses/by/4.0/legalcode (record license field: `cc-by-4.0`).
- **Scope used**: 20 WAV files (~22 MB) of the 3613-file 2.08 GB archive, redistributed under
  CC BY 4.0 with this attribution. Modifications: 6 files trimmed to 15.0 s from t=0; no other
  processing. Per-file provenance (SHA-256) in `docs/corpus/manifest.jsonl`.

## Annotated-VocalSet (annotations)
- **Used for**: reference F0 / note boundaries for the clips above (`docs/corpus/annotations/*`),
  converted from the source 17-column format to the repository 4-column schema
  (`t_start_s,t_end_s,f0_hz,midi`).
- **Source**: Behnam Faghih, Joseph Timoney, "Annotated-VocalSet: A Singing Voice Dataset",
  Zenodo, https://doi.org/10.5281/zenodo.7061507; paper: Faghih & Timoney,
  Applied Sciences 12(18):9257, 2022, https://doi.org/10.3390/app12189257 .
- **License**: Creative Commons Attribution 4.0 International (CC BY 4.0).
