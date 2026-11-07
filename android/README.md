# StreamFetch Android

Native Android edition of StreamFetch. Built with **Kotlin** and **Jetpack Compose**.

## Features
- **Zero Python dependency**: Native stream extraction (~7.4 MB APK size).
- **Zero intrusive permissions**: Uses `INTERNET` permission only.
- **Dynamic storage selection**:
  - Default: Saves to system public `Downloads/` folder via Android `MediaStore`.
  - Custom: User can pick any folder via Android Storage Access Framework (`OpenDocumentTree`).
- **Share sheet integration**: Receives shared YouTube links directly from YouTube app.
- **Format detection**: Lists 360p, 720p, 1080p progressive streams.
- **Looping download animation**: Smooth visual feedback while download is active.

## How to Run

1. Open **Android Studio**.
2. Select **Open** and choose the `android` folder:
   `c:\Users\karth\Downloads\yt downloader\android`
3. Wait for Gradle sync to complete.
4. Select your running emulator device in the top bar.
5. Click **Run** (`Shift + F10` or green play button).

---

## Legal Disclaimer & Compliance

This software is developed strictly for educational, research, and personal archival purposes.

- **Content Rights:** Users are solely responsible for ensuring they have the legal right to access and download media. Only download media you own, public domain works, or content under open licenses (e.g., Creative Commons).
- **Platform Terms:** Automated media retrieval may violate platform terms of service. Authors assume no liability for misuse, account restrictions, or copyright infringement.
- **No DRM Circumvention:** StreamFetch does not decrypt, bypass, or circumvent DRM protection technologies.
- **Trademark Notice:** StreamFetch is an independent project and is not affiliated with, authorized, maintained, or endorsed by YouTube or Google LLC.
