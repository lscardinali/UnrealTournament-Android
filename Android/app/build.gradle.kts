plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.lscardinali.unrealtournament"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "com.lscardinali.unrealtournament"
        minSdk = 28
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk { abiFilters += "arm64-v8a" }
    }

    // The glibc loader (libld-linux.so) must be extracted to nativeLibraryDir so it can be exec'd.
    packaging {
        jniLibs { useLegacyPackaging = true }
    }

    buildTypes {
        release {
            // Sideloading: signed with the debug key (same signature as debug builds, so it installs over them).
            // A real keystore is only needed for distribution.
            signingConfig = signingConfigs.getByName("debug")
            optimization {
                enable = true
                packageScope = setOf("androidx.**", "kotlin.**", "kotlinx.**")
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
}

dependencies {
    // Termux:X11's X server and display activity (third_party/termux-x11; see settings.gradle.kts).
    implementation(project(":lorie"))
    implementation("androidx.appcompat:appcompat:1.7.1") // GameActivity extends Termux:X11's AppCompatActivity
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    // Setup: the 469 patch (.tar.bz2) and Bonus Pack 4 (.7z, LZMA2 via xz).
    implementation("org.apache.commons:commons-compress:1.27.1")
    implementation("org.tukaani:xz:1.10")
    testImplementation(libs.junit)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.androidx.junit)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
    debugImplementation(libs.androidx.compose.ui.tooling)
}