# RC3.7 status and next steps

## Acceptance scope

The private RC3.7 4 GiB image was fully written, independently read back and booted on the original GKD Mini. Its SHA-256 is `a0ef64d8a38b010375be326bd72a7d37172748f8bdd5e4017fc4d022165d6f04`. The final image contains matching built-in and persistent routing configuration, configuration-driven portrait X-to-original-A routing, dot-to-Y hold autofire, and full-basename English menu aliases. The image and device identity material are not distributed here.

After the final whole-image write, 1943 and Gunpey passed portrait routing, hold/release and native exit checks; Art of Fighting 2 passed landscape routing and exit checks. Truxton and Raiden passed with the same A slot and routing configuration before the final persistent-partition packaging. The private build manifest matched all 478 participating source files during closeout. The normalized public source has a different source identity and is not evidence that a separate public build matches the installed image.

Earlier accepted RC3.6 base checks covered owner-confirmed audio and physical keys, sleep/wake, settings, USB modes and Internet, physical game-card removal/reinsertion, A/R boot and representative games across 22 platforms. Those are historical base results, not a claim that every item was repeated against the final RC3.7 image. Every ROM, blank-card installation and forced-power-loss recovery have not been accepted in this closeout. Low-battery protection was exercised using safe simulated thresholds rather than deliberately exhausting the battery.

## Next-stage work

- Replacement frontend and its migration/UX acceptance.
- ROM scanning and metadata integration.
- Specific USB network adapter support and hardware acceptance.
- Independent frontend upgrades.
- Redistributable firmware: dependency/license and security review, release keys, clean-card image and writer acceptance.
- Persistent Windows automatic drive-letter assignment; the accepted test used a manually assigned letter.
- Controlled power-loss and recovery testing.

Card-health scoring is cancelled, not pending work. This public source repository is the first part of publication preparation, not completion of the firmware-distribution tasks above.
