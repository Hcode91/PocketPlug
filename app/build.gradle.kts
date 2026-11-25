plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.hcode91.pocketplug"
    compileSdk = 34
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = "com.hcode91.pocketplug"
        minSdk = 26
        targetSdk = 34
        versionCode = 101
        versionName = "0.78.0"
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_static" } }
    }

    // One fixed signing key (kept as GitHub secrets, decoded by the workflow) so every build
    // installs over the last one; without it builds fall back to the runner's throwaway debug key.
    val ppKeystore = System.getenv("PP_KEYSTORE")?.let { file(it) }?.takeIf { it.exists() }
    signingConfigs {
        if (ppKeystore != null) create("pocketplug") {
            storeFile = ppKeystore
            storePassword = System.getenv("PP_KEYSTORE_PASSWORD")
            keyAlias = "pocketplug"
            keyPassword = System.getenv("PP_KEYSTORE_PASSWORD")
        }
    }
    val ppSigning = if (ppKeystore != null) signingConfigs.getByName("pocketplug") else signingConfigs.getByName("debug")

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = ppSigning
        }
        debug {
            signingConfig = ppSigning
        }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt") } }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    packaging { resources { excludes += "META-INF/*" } }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    // ROM set import: .zip is handled by the JDK, .7z needs commons-compress + xz
    implementation("org.apache.commons:commons-compress:1.26.2")
    implementation("org.tukaani:xz:1.9")
    testImplementation("junit:junit:4.13.2")
}
