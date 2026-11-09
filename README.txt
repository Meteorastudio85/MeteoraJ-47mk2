J-47 MK2 (Meteora Plugins) - registratore a bobine - VST3 per Windows, VST3 e AU per macOS
=========================================================================================

COMPILARE SU GITHUB
1. Estrai lo zip. Nel repository premi "Add file -> Upload files", seleziona tutto (Ctrl+A, compresa la
   cartella .github) e trascinalo. Commit changes. Il workflow trova da solo CMakeLists.txt.
2. Actions -> partono due compilazioni insieme. Alla spunta verde scarichi:
   - "J-47-MK2-Windows-VST3"   -> J-47 MK2.vst3
   - "J-47-MK2-macOS-VST3-AU"  -> J-47 MK2.vst3 e J-47 MK2.component (Mac Intel e Apple Silicon)

INSTALLARE
Windows: chiudi la DAW, copia "J-47 MK2.vst3" in C:\Program Files\Common Files\VST3 e CANCELLA il vecchio "Tape J-47.vst3"
         (e' lo stesso plugin con un nome nuovo: se restano tutti e due la DAW si confonde).
macOS:   copia  J-47 MK2.vst3        in  /Library/Audio/Plug-Ins/VST3
                J-47 MK2.component   in  /Library/Audio/Plug-Ins/Components
         La prima volta (plugin non firmato da Apple), nel Terminale:
           xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/J-47 MK2.vst3"
           xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/J-47 MK2.component"

VECCHI PROGETTI
Stesso plugin del Tape J-47 (e del vecchio Meteora Tape) con un nome nuovo e gli stessi codici: i progetti salvati si riaprono con lo stesso suono (canale 1, nastro in PLAY,
plugin acceso). SATURATION ora si chiama INPUT LEVEL e MAKEUP GAIN si chiama OUTPUT LEVEL (stessi valori).
La vecchia levetta LO-FI accesa diventa LO-FI al 100%. Il BIAS su UNDER (non c'e' piu') riparte da 0%.

COMANDI
- INPUT LEVEL: spinge il nastro (-12 / +24 dB). OUTPUT LEVEL: -24 / +12 dB. Collegati: girando INPUT LEVEL,
  OUTPUT LEVEL scende della stessa quantita' (piu' saturazione, stesso volume); poi lo ritocchi da solo.
- SPEED SELECTOR 7.5 / 15 / 30 IPS: clic vicino al numero. La bobina di destra cambia colore:
  oro a 7.5, blu a 15, rossa a 30.
- BIAS (0-100%): 0% = taratura normale; salendo il nastro diventa piu' scuro, morbido e pulito (over-bias).
- PLAY / STOP: con STOP il nastro rallenta e si ferma (il suono scende di tono e si spegne), con PLAY riparte.
  Le bobine rallentano e ripartono insieme al suono. Girano fluide e fanno giri completi: la bobina piena
  (sinistra) gira piu' piano, quella quasi vuota (destra) piu' veloce, come su un registratore vero. Automatizzabili dalla DAW ("Play").
  Tempi regolabili dalla DAW: Stop Time (0.2-5 s, 1.5 s) e Start Time (0.1-3 s, 0.5 s).
- RECORD: sempre acceso rosso, solo decorativo.
- VU con il vetro: quando il nastro satura (da +6 VU) i quadranti diventano rossi.
- Pulsante tondo: acceso / spento (bypass morbido). Spento: VU al buio, bobine ferme, suono pulito.
- Pomelli del trasporto: a sinistra il grande e' DRY/WET; a destra il grande e' HISS, il piccolo LO-FI (0-100%).
- CANALE (contagiri): ogni copia del plugin ha il suo canale (1-64), con un nastro un filo diverso dagli altri
  (alti, bassi, valvola, oscillazione, fruscio): messo su piu' tracce il mix prende profondita'.
  Ogni copia nuova prende da sola il primo canale libero (1, 2, 3...). Cambi canale con il tastino nero accanto
  (sopra = su, sotto = giu') o con la rotella del mouse sulle cifre. Il canale 1 e' il suono di sempre.
- VU: a sinistra il nastro in ingresso, a destra l'uscita. 0 VU = -18 dBFS.
- Doppio clic su un pomello = posizione di partenza. Finestra quadrata: si apre a 900 x 900,
  ridimensionabile dall'angolo o col clic destro.
- Comandi morbidi: girando i pomelli non ci sono click ne' "zipper".

NOTE
- Il numero automatico del canale funziona tra le copie aperte nella stessa DAW. Se la DAW apre ogni plugin
  in un processo separato (es. Bitwig in modalita' "sandbox" per plugin), il canale va scelto a mano.

LICENZA: Copyright (c) 2026 Meteora Plugins - vedi LICENSE.txt e THIRD_PARTY_NOTICES.txt.
Se la build da' errori, manda il log.
