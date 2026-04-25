# Auto Deafen on MacOS!

Have you ever been far in a level and wanted to FOCUS (limbo reference) when an annoying friend arrived and screamed some stupid things, making you die?
Now that's no longer a problem! Choose when you want to focus and beat your next hardest without your social life becoming your biggest choke point!


Huge thanks to Inverseds for his feedback and help during all this project! Mod inspired from Lynxdeer's one on windows.
## Usage
In the pause menu, an extra button is added to the top-left of the screen. 
If it its your first time using this mod, the button will open a menu to connect your discord application to your account, which will give the application the ability to deafen/undeafen you.
(More detailed explanations of the setup below)
If this was already done previously, the button will open a menu where you can set when you would like to be deafened and when you would like to be undeafened. 
Another "settings" button is also available on the top-right of the auto deafen menu. There, you can activate/deactivate the auto deafen mod, and modify parameters (more will be available soon, probably). However, you can also (if you'd like to) reconnect your discord application to your discord account (people mainly do this when they reset their secret key on the developer portal, since you can only see it once).

## How to connect
Unfortunately, it's a bit hard to setup the mod... Follow this step by step guide and normally this will work perfectly!
### This only works with the discord app, not the website!

### 1.Create a discord app on the discord developer portal (https://discord.com/developers/home)
1. Click on the big create button at the up right of your screen and choose new blank application
2. Choose a name for the app (it can be anything, it doesn't matter)
3. Create the app, you will normally be sent in a website where you can modify lot of parameters for your app (you can choose a profile picture if you want)

### 2. Setup your app
4. Click on the Oauth2 tab on the left of the screen
5. Click on "add a redirection URL"
6. Tap this URL: http://localhost:8000 (http, not https!!)
7. Scroll down and check the scopes "rpc" and "rpc.voice.write"
8. At the bottom, select the previously written URL (http://localhost:8000)

### 3. Link Discord and GD
9. Copy the client id of the app (at the top of the webpage)
10. Launch GD with the mod, enter a level and enter pause menu: click on the mod button (the headphones) and click on the button saying "paste client id"
11. Go back on the website and click on "regenerate client secret". (maybe you will need to verify your identity)
12. Copy the client secret and on gd click on "paste client secret"
13. Press the "connect" button
14. Now you are all done! Setup the deafen percentage and enjoy the mod! To understand how to use the mod read the Usage section above.
