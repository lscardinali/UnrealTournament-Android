pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}
plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "Unreal Tournament"
include(":app")
 
// Termux:X11's X server (third_party/termux-x11, patched by tools/setup-x11.sh), as in pocket-foundry.
include(":lorie")
project(":lorie").projectDir = file("third_party/termux-x11/lorie")
// Termux:X11 stubs that :lorie compiles against. The parent dir is empty on purpose (see its README.md).
include(":shell-loader:stub")
project(":shell-loader").projectDir = file("third_party/shell-loader-parent")
project(":shell-loader:stub").projectDir = file("third_party/termux-x11/shell-loader/stub")
