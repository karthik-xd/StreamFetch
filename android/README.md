# StreamFetch Android

Native Android edition of StreamFetch. Built with **Kotlin**, **Jetpack Compose**, and **NewPipeExtractor**.

## Features
- **Zero Python dependency**: Native Java/Kotlin stream extraction (~10 MB APK size).
- **Zero intrusive permissions**: Uses `INTERNET` permission only.
- **Dynamic storage selection**:
  - Default: Saves to system public `Downloads/` folder via Android `MediaStore`.
  - Custom: User can pick any folder via Android Storage Access Framework (`OpenDocumentTree`).
- **Share sheet integration**: Receives shared YouTube links directly from YouTube app.
- **Format detection**: Lists 360p, 720p, 1080p progressive streams.

## How to Run

1. Open **Android Studio**.
2. Select **Open** and choose the `android` folder:
   `c:\Users\karth\Downloads\yt downloader\android`
3. Wait for Gradle sync to complete.
4. Select your running emulator device in the top bar.
5. Click **Run** (`Shift + F10` or green play button).
