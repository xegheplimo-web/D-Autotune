plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.dautotune.app"
    compileSdk = 36
    // Pinned to the NDK CI installs (.github/workflows/ci.yml) so host and
    // CI builds use the same toolchain.
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.dautotune.app"
        minSdk = 30 // Android 11
        targetSdk = 36 // Play 2026
        versionCode = 1
        versionName = "0.1.0-alpha"

        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-O3", "-ffp-contract=fast")
                // Prefab CXX1212: Oboe AAR is built with the shared STL; the app must
                // match it (default would be c++_static). libc++_shared.so is bundled
                // automatically by AGP for both ABIs.
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            isDebuggable = false
        }
    }
    buildFeatures { prefab = true }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1" // matches cmake_minimum_required + CI component
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    packaging { jniLibs { useLegacyPackaging = false } }
}

dependencies {
    implementation("com.google.oboe:oboe:1.11.0")
}
