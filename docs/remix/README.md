# Advance Wars 2 - Remix

A patch set for *Advance Wars 2: Black Hole Rising (USA)*: quality-of-life tweaks, a small balance pass and five remixed Orange Star missions. It is built from your own ROM, so no game data is stored here.

| doc | content |
|---|---|
| [BUILD.md](BUILD.md) | how to build the ROM and BPS patch, and how to boot any mission for testing |
| [MAP_FORMAT.md](MAP_FORMAT.md) | the plain-text map format the remixed maps are written in |
| [BALANCE.md](BALANCE.md) | the CO and unit balance changes |
| [ROM_NOTES.md](ROM_NOTES.md) | ROM structures: maps, units, COs, free space |

The map sources are in `tools/remix/maps/<id>_<slug>.txt`.

## Missions

Missions 1–5 of the Orange Star campaign are remixed as a veteran challenge, harder than the stock Advance (hard) campaign.

- **Same starting force.** Your starting units and their positions are unchanged, and so are the HQs, the map sizes, the COs and the day limit. Scripted events and Cleanup's tutorial depend on them.
- **More and better enemies.** Each mission adds enemy units, most of them indirect-fire or dug in.
- **Dug in.** Enemy-held cities give healing and 3-star cover, and the enemy sits on mountains and woods in front of its lines. Garrison units use the `ai 1` behaviour byte, which is believed to mean "hold position" (see MAP_FORMAT.md).
- **No new production.** Neither side gains a base or airport, so the units listed are the whole enemy force.
- **Some help for you.** Missions that add many enemies also add a few Orange Star units, so the odds stay beatable.

| # | mission | CO (you vs enemy) | what changed |
|---|---|---|---|
| 1 | **Cleanup** | Andy vs Flak | Black Hole holds the river line with 10 units, including Mechs on the mountains over the ford, 3 enemy cities, Artillery covering the bridge road and a fast Recon flanker. You get a Mech pair and an Artillery. The tutorial is intact. |
| 2 | **Border Skirmish** | Sami vs Flak | **Fog of war.** The bridge exit is a hill fort: mountains, woods and two extra enemy cities, with Artillery and Rockets covering the bridge, and a Md Tank in reserve (13 enemy units). You get a Md Tank, Rockets and a Recon. |
| 3 | **Orange Dawn** | Max vs Flak | **Fog of war.** The enemy holds the north bridgehead and a forward city. Rockets and Artillery cover both bridges, an Anti-Air shoots at your copters, and Mechs are dug in on the hill (14 enemy units). You get an Anti-Air and a Recon. |
| 4 | **Flak Attack** | Max vs Flak | The air armada grows to 9 flyers (extra Fighter and Bomber), Artillery and Rockets cover the north bridge, and an enemy outpost holds the bridge-head city. The far HQ is garrisoned with Mechs and Artillery (18 enemy units). You get an Anti-Air and a Fighter. |
| 5 | **Lash Out** | Sami vs Lash | The 7-day limit stays. Two Artillery and Rockets behind the HQ shell the landing beaches, the NE island cities are Black Hole's, and the enemy adds a Cruiser, a Sub and a Md Tank that attacks down the west road (16 enemy units). Your Battleship outranges the Rockets by one tile. |

Notes for players:

- Each remix keeps the stock mission text and portraits. Only the map terrain, the unit list and the fog flag are changed.
- The balance pass in [BALANCE.md](BALANCE.md) applies as well. Flak's luck range is wider, which makes missions 1–4 slightly harder.
