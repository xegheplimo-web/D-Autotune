plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.dautotune.app"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.dautotune.app"
        minSdk = 30 // Android 11
        targetSdk = 36 // Play 2026
        versionCode = 1
        versionName = "0.1.0-alpha"

        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild {
            cmake { cppFlags += listOf("-std=c++17", "-O3", "-ffp-contract=fast") }
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
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
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
