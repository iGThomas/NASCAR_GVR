# Changing the menu textures

The arcade menus keep their textures inside the menu scenes, the `.am` files in the `Shell` folder. Examples: the tires and paint of the 3D car in driver select, logos, buttons and backgrounds.

The menu texture editor shows every texture and lets you replace it with your own. Your textures are kept when the game updates, and you can go back to the original at any time.

The race has its own texture files and is not covered here.

## Open the editor

1. Open the game's install folder, for example `C:\Games\NASCAR_GVR_1.1`.
2. Open the `Tools` folder.
3. Double-click `GvrMenuTextures.exe`.

The game does not need to be running.

## Find the texture

1. Choose a **Menu scene** at the top. The editor starts with `NASCAR_Selection`. Scenes without textures are not listed.
2. The line below it lists the groups in that scene, such as `DriverSelect`, `TrackSelect` or `Lobby`. The groups come from the scene itself: they are the folders the original artists kept the textures in. So they tell you which screens the scene contains.
3. Scroll through the thumbnails. Each one shows its number and its name from the scene, for example `17 CarSkin16`. Textures in the `3D models` group belong to 3D objects, such as the steering wheel.
4. Click a texture to see it larger on the right.

Not sure which texture you need? Click **Save all textures...** and look through them in a folder.

## Edit the texture

1. Click the texture, then **Save a copy to edit...**.
2. Open the copy in any paint program, for example Paint.NET, GIMP or Photoshop.
3. Make your changes. Keep the same width and height. Keep the transparent parts transparent.
4. Save it as PNG.

## Put it in the game

1. Click the same texture in the editor.
2. Click **Replace with my texture...** and choose your file.
3. Start the game. Your texture is in the menus.

If your texture has a different size, the editor offers to resize it.

Changed textures have a `*` before their number. **Show only replaced textures** lists only those.

## Example: the tires of the menu car

In NASCAR Racing, the tires, rims and some interior parts of the 3D car in driver select share one texture. It is in `NASCAR_Selection`: texture 9, named `Universal_ps2`, in the `DriverSelect` group.

1. Open the editor and click texture 9.
2. Click **Save a copy to edit...**.
3. Paint the tires and rims as you like, and save.
4. Click **Replace with my texture...** and choose your file.
5. Start the game and choose a driver.

The car paints of the 3D car are in the same scene, named `CarSkin` and the car number. Texture 17, `CarSkin16`, is the #16 car.

## Export and import textures

1. Click **Export my textures...**. The editor saves all your replaced textures in one zip file, a texture pack.
2. Send the zip to other players of the same game.

To use a pack from someone else:

1. Click **Import a texture pack...** and choose the zip.
2. If you already replaced some of the same textures, the editor asks whether to keep yours.
3. The editor shows what it added and what it skipped. Start the game to see the textures.

A pack made for the other game is skipped texture by texture. Nothing in your install changes then.

## Go back to the original

Click the texture, then **Use the original again**. The original is back the next time you start the game.

## Good to know

- **True colours, right way up.** The scenes store textures upside down. PNG textures also have red and blue swapped. The editor shows and saves them the normal way, and turns them back when it puts them in.
- **Limited space.** Each texture has a fixed amount of space in the scene. If your texture is too big, the editor first tries fewer colours or a stronger JPEG compression. A very detailed texture may not fit. Then the editor says so.
- **Tiny textures.** A few textures are only 8 x 8 pixels and cannot be replaced. They are plain colour fills.
- **Where your textures are.** The editor keeps them in `Shell\Mods\<scene>\`, named like `009_Universalps2_97CA4A03.png`. Keep the number at the start and the code at the end: they tell the game which texture the file replaces. You can also edit these files directly. The game picks up the change at the next start.
- **Updates and removal.** Updates keep your textures. Uninstall copies them to `Documents\NASCAR_GVR Backup` first.

## If a texture does not change

Open `LOG\gvrmenutextures.log` in the install folder. Each start lists which textures went in, and why a texture was skipped.
