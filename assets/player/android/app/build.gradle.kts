// A game made with Diorama, as an Android app: the game (the same as its website) inside, shown full screen.
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "diorama.game"
    compileSdk = 34

    defaultConfig {
        applicationId = "{{PACKAGE}}"
        minSdk = 26
        targetSdk = 34
        versionCode = {{VERSION_CODE}}
        versionName = "{{VERSION}}"
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    androidResources {
        noCompress += listOf("glb", "mp4", "webm", "mp3", "ogg", "wav", "wasm", "png", "jpg")
    }
}
