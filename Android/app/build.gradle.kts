import java.util.Properties

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

    // One app per game, same code (setup/Product.kt picks the game from BuildConfig.GAME):
    //   ut      Unreal Tournament (OldUnreal 469)            com.lscardinali.unrealtournament
    //   unreal  Unreal Gold (OldUnreal 227, XOpenGL on GLES)  com.lscardinali.unreal
    flavorDimensions += "game"
    productFlavors {
        create("ut") {
            dimension = "game"
            isDefault = true
            buildConfigField("String", "GAME", "\"ut\"")
        }
        create("unreal") {
            dimension = "game"
            applicationId = "com.lscardinali.unreal"
            buildConfigField("String", "GAME", "\"unreal\"")
        }
    }

    // Native libraries extracted to nativeLibraryDir (smaller download; the loader dlopens them by name).
    packaging {
        jniLibs { useLegacyPackaging = true }
    }

    // Release signing: Android/keystore.properties (storeFile, storePassword, keyAlias, keyPassword), kept out of git.
    // Without it, release builds fall back to the debug key so they still build.
    val releaseKeys = rootProject.file("keystore.properties").takeIf { it.exists() }?.let { f ->
        Properties().apply { f.inputStream().use { load(it) } }
    }
    signingConfigs {
        if (releaseKeys != null) create("release") {
            storeFile = rootProject.file(releaseKeys.getProperty("storeFile"))
            storePassword = releaseKeys.getProperty("storePassword")
            keyAlias = releaseKeys.getProperty("keyAlias")
            keyPassword = releaseKeys.getProperty("keyPassword")
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
            // Maintenance only (./gradlew assembleRelease -PdebuggableRelease): lets adb run-as into a release install,
            // e.g. to restore saves; an update with the normal build keeps the data.
            isDebuggable = project.hasProperty("debuggableRelease")
            // Whole-app R8 (keep rules in src/main/keepRules/rules.keep: JNI entry points and SDL's Java side).
            // A package-scoped optimization of only androidx/kotlin broke Kotlin classes at runtime
            // (IllegalAccessError in ProcessLifecycleInitializer).
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
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