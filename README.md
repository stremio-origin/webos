![Stremio Origin](docs/header-rounded.png)

<p align="center">
  <img src="docs/logo-pill.png" width="200" alt="Stremio Origin" align="middle">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="docs/platform-lg.svg" height="72" alt="LG" align="middle">
</p>

<h1 align="center">Stremio Origin for LG webOS</h1>

<p align="center">A native app for Stremio addons, written in C.</p>

<p align="center"><em>Stremio™ is a trademark of its owner. Stremio Origin is independent and unofficial, and is not affiliated with, endorsed by or sponsored by Stremio. LG is a trademark of its owner; its logo only shows which platform this is for.</em></p>

<p align="center">You bring your own Stremio account and your own addons. The app ships empty and plays only the stream links your addons return.</p>

## Features
- Seek previews: a picture of the film on the seek bar, for debrid and direct links
- Trakt: link with a code, scrobbling, rows on Home, watched marks and Continue Watching from Trakt
- Home, Discover, Library and Search, with the addons from your Stremio account
- Film collections in order, in the Library too, and the next film after the credits
- Live TV and sports from your addons, with All channels, Favorites, Recent and a preview as you browse
- The best stream picked in one press, and a sources tray with a tab for each addon
- Styled subtitles as the film made them, with fonts (Arabic too), size, colour, position, brightness and sync
- Forced-only subtitles; each show remembers its subtitles and audio, original language by default
- Chapters, skip intro and recap, and a next-episode card in two sizes
- Auto-play next episode, and a trailer behind Home
- Spoilers blurred for episodes you haven't watched, and upcoming episodes on Home
- A viewer notice as a film starts: an age and its reasons, from IMDb's Parents Guide
- A row for every catalog, with artwork, logos, trailers, people, reviews and ratings
- Titles and descriptions in your language
- Library, watched state and Continue Watching synced with your Stremio account
- Accent colour, a 12 or 24 hour clock and interface sounds
- Dolby Vision on TVs that support it
- DTS sound converted on the TV
- Torrents, with an optional USB drive
- Picture size: original, fill or zoom
- The app's own screen saver
- The pointer hides when idle, and OK presses what you point at
- 28 languages, Arabic included (right to left); updates and Share logs from the app

## Install
LG TVs on webOS 3 or later (ARM). With the Homebrew Channel, search for Stremio Origin. If it doesn't show up, add the repository `https://stremio-origin.github.io/webos/apps.json`. Or install the `.ipk` from [Releases](https://github.com/stremio-origin/webos/releases) with the TV in Developer Mode.

## Compatibility
Developed and tested on webOS 4.4, and reported working on webOS 4 to 25. [More](docs/compatibility.md).

## Privacy
- Sign-in goes directly to Stremio. Your account stays on the TV.
- No analytics. Nothing is sent to the developer.
- Logs stay on the TV. Sharing them is your choice: Settings > About > Share logs.

## Trakt
Trakt is optional. You link it with a code and the sign-in stays on the TV. While it is linked, what you watch is added to your Trakt history. The app also reads your Trakt name and stats, history, watchlist, recommendations, watched marks and paused positions, only to show them in the app. Unlink any time: Settings > Trakt > Disconnect Trakt.

Stremio Origin uses Trakt but is not endorsed or certified by Trakt.

## Support
- <img src="docs/heart.svg" height="18" align="top"> Support the project / Request a feature: [Ko-fi](https://ko-fi.com/simussu) <img src="docs/heart.svg" height="18" align="top">
- <img src="docs/discord.svg" height="18" align="top"> [Discord](https://discord.gg/SJEtyV3kW6)
- Report bugs/issues: [stremio-origin/webos/issues](https://github.com/stremio-origin/webos/issues)

## Credits
- Interface sounds: [Interface Sounds](https://kenney.nl/assets/interface-sounds) by Kenney (CC0).
- The glass look: [liquid-glass-react](https://github.com/rdev/liquid-glass-react) by rdev; the app ships a modified version.
- Torrents: [Nuvio Engine](https://github.com/NuvioMedia/nuvio-engine) (GPL-3.0); the app ships a modified version.

## Licence
GPL-3.0, see [LICENSE](LICENSE). Third-party components and their licences: [third_party/licenses/README.txt](third_party/licenses/README.txt).

Provided as is, for personal use on devices you own.
