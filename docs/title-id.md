# Title ID choice: PPSA93303

Checked 2026-10-28 ~23:58 America/New_York.

| Source | How | Result |
|---|---|---|
| github.com/damiantw/ps5-homebrew-catalog (main) | git clone, regex `[A-Z]{4}[0-9]{5}` over all files | 85 IDs, PPSA93303 absent |
| github.com/blackbearreloaded/ps5-homebrew-catalog + ProsperoStore | shallow clone, same regex | absent |
| recompstore.com (Recomp Store front page, all listed titles) | curl, regex | 155 IDs, absent. Recomps sit in PPSA9xxxx; PPSA93003 = Skate 3 (taken), PPSA93004/93007 taken |
| Web search "PPSA93303" | WebSearch | no PS5 hits |
| PPSA99320 | | PS5_Shipwright, ours |

Union list: known-title-ids.txt (213 IDs). Retail PS5 PPSA IDs are well below 9xxxx. Re-check right before release (the catalogs move). Still not checked: GitHub code search (gh token on the box returns 401) and the homebrew.page UI listing.
contentId proposal: `UP9000-PPSA93303_00-SF3RECOMP0000000` (19+16 chars like Shipwright's).
