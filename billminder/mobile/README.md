# BillMinder Android Application

This folder contains a local-first React Native (Expo) application designed specifically to run on your personal Android device. It uses the BillMinder Core Service's REST API and natively fulfills the requirement of retaining **zero confidential information** upon app exit.

## Architecture Highlights
- **Framework**: React Native with Expo SDK 54.
- **Networking**: Configured to permit plain HTTP cleartext traffic for local home server usage (`app.json` -> `usesCleartextTraffic`).
- **Data Privacy**: No API responses are cached to disk. Additionally, a global AppState listener aggressively clears in-memory React state arrays when the application is minimized or put into the background.

## Setting Up on Other Phones

If you have a new Android device and want to run the app again, follow these simple steps:

### 1. Match the Expo Go Version
1. On your Android phone, download **Expo Go** from the Google Play Store.
2. The current version of this project is strictly pinned to **Expo SDK 54**, which means the latest Expo Go app from the Play Store will work perfectly.

### 2. Start the Metro Bundler
On the machine hosting your BillMinder repository, open a terminal, navigate to this `mobile` directory, and start the development server:

```bash
cd billminder/mobile
npx expo start
```

*Note: Make sure your BillMinder backend C++ service is also running (e.g., `./start.sh` in the root folder).*

### 3. Connect Your Phone
1. Ensure your Android phone is on the **same Wi-Fi network** as your host computer.
2. Open the **Expo Go** app on your phone.
3. Tap **Scan QR Code** and scan the code displayed in your terminal.
4. The application will immediately bundle and open on your device.

### 4. Configure the API Server
1. By default, the app will try to connect to a placeholder local IP.
2. Tap the **Settings (⚙️) icon** in the top right corner of the Dashboard.
3. Enter the exact IP address and port of your BillMinder Core Service. 
   - *Example: `http://192.168.1.100:8080/api`*
   - Make sure you include the `http://` and `/api` suffix.
4. Tap **Save Settings** and return to the Dashboard.

## Troubleshooting

### "The SUID sandbox helper binary was found, but is not configured correctly"
If you accidentally press **`j`** in the terminal running `npx expo start`, Expo will try to open the standalone React Native DevTools. On Linux systems, this can crash due to Chrome sandbox permissions or spaces in the file path.
- **Solution**: You don't need DevTools to run the app. If you hit this error, just ignore it. The Expo Metro bundler is still running in the background and you can safely scan the QR code on your phone.
- **Alternative Debugging**: Press **`m`** to open the developer menu on your phone, and select "Open JS Debugger" to debug directly in Chrome.

### Project is Incompatible with Expo Go
This happens if you attempt to use an old phone that cannot download the latest Expo Go. To fix this, you must explicitly downgrade the `package.json` Expo dependencies to match your phone's supported SDK version, and run `npx expo install --fix`. (The repository currently ships downgraded to SDK 54 specifically to match your device).
