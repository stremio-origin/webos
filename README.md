![Stremio Origin](docs/header-rounded.png)

<p align="center">
  <img src="docs/logo-pill.png" width="200" alt="Stremio Origin">
</p>

<h1 align="center">Stremio Origin</h1>

<p align="center">A native TV app for Stremio addons, written in C.</p>

<p align="center"><em>Stremio™ is a trademark of its owner. Stremio Origin is independent and unofficial, and is not affiliated with, endorsed by or sponsored by Stremio. The name only describes compatibility.</em></p>

<p align="center">You bring your own Stremio account and your own addons. The app ships empty, hosts and indexes nothing, and plays only the stream links your addons return.</p>

## Features
- Home, Discover, Library and Search from your addons, with artwork, logos and trailers
- The best stream for your TV and connection, picked in one press
- Buttons and highlights take their colour from each title's logo or artwork
- People, reviews and a spotlight of what's new on every title
- A player with episodes, sources, subtitles and audio, skip intro and recap, and a next-episode card
- Styled subtitles as the film made them, subtitle sync, and optional forced-only subtitles in a chosen language
- DTS sound converted on the TV, and Dolby Vision on TVs that support it
- Torrent playback with a buffer sized to your TV, and an optional USB drive
- Cast to the TV from your phone (the Android app is almost done)
- Library, watched state and Continue Watching synced with your Stremio account
- 11 languages, including Arabic (right to left)
- Updates from inside the app

## Install
For LG TVs running webOS 3 and later (ARM). It should work on all of them, but it has only been tested on webOS 4.4. [Compatibility](docs/compatibility.md)

- With the Homebrew Channel: add the repository `https://stremio-origin.github.io/webos/apps.json`,
  then install Stremio Origin.
- Or download the `.ipk` from Releases and install it on a TV in Developer Mode.

## Privacy
- Sign-in goes directly to Stremio. Your account stays on the TV.
- No analytics. Nothing is sent to the developer.
- Logs stay on the TV. Sharing them is your choice: the app seals them and shows a short code.

## Support
- Issues: https://github.com/stremio-origin/webos/issues
- Support: https://ko-fi.com/simussu

## Credits
- Interface sounds: [Interface Sounds](https://kenney.nl/assets/interface-sounds) by Kenney (CC0).
- The glass look: [liquid-glass-react](https://github.com/rdev/liquid-glass-react) by rdev; the app ships a modified version.
- Torrents: [Nuvio Engine](https://github.com/NuvioMedia/nuvio-engine) (GPL-3.0); the app ships a modified version.

## Licence
GPL-3.0, see [LICENSE](LICENSE). Third-party components and their licences: [third_party/licenses/README.txt](third_party/licenses/README.txt).
