# Frontend do `romsel_dsimenutheme` — carrossel, cursor e render

Documentação da mecânica do ROM selector (tema DSi) do TWiLightMenu, para servir de base ao
rework. Arquivos-chave: `arm9/source/fileBrowse.cpp` (dados + input + launch), `arm9/source/graphics/graphics.cpp`
(render, `vBlankHandler`/`frameRateHandler`), `arm9/source/iconTitle.cpp` + `graphics/iconHandler.*`
(ícones), `arm9/source/graphics/ThemeTextures.cpp` (backgrounds/sprites), `arm9/source/main.cpp`
(boot dos jogos, **fora** do frontend).

## 1. Telas e engines

- **Tela de cima = engine SUB** (`BG_GFX_SUB`), background via `tex().drawTopBg()` (textura de fundo índice 0), buffer `_bgSubBuffer`.
- **Tela de baixo = engine MAIN** (`BG_GFX`), background via `tex().drawBottomBg(index)` (índices 1/2/3), buffer `_bgMainBuffer`.
- O **3D (gl2d)** roda na MAIN e é exibido na **tela de baixo** (via lcd swap). Os **ícones/cursor** são sprites gl2d desenhados sobre o BG da tela de baixo.
- Camadas da tela de baixo (de trás pra frente): BG (wallpaper) → 3D gl2d (ícones, boxes, brace) → camada de fonte (texto, BG2).
- Backgrounds hoje: **cor sólida** (`SOLID_BG_COLOR` em `ThemeTextures.cpp`) em vez do bitmap do tema, tanto em `drawTopBg` quanto em `drawBottomBg`.

## 2. Modelo de dados da listagem

- **`dirContents[scrn]`** (`vector<DirEntry>`): itens do diretório atual (`name`, `isDirectory`). `scrn` = `SwitchState` (dispositivo).
- **`file_count`** = `dirContents[scrn].size()`.
- **Paginação de 40** via **`PAGENUM`** (`ms().pagenum[secondaryDevice]`): índice global = **`CURPOS + PAGENUM*40`**.
- **`CURPOS`** (`ms().cursorPosition[secondaryDevice]`, macro em `fileBrowse.h`): seleção **dentro da página** (0..`last_used_box`). É a **fonte da verdade** para launch e config.
- **`last_used_box`** = `clamp(file_count-1-PAGENUM*40, 0, 39)`.
- **`spawnedtitleboxes`**: nº de boxes materializados na página.
- **Arrays paralelos por item** (índice 0..40; 40 = slot do "moving app"): `isDirectory`, `bnrRomType`, `isDSiWare`, `unitCode`, `gameTid`, `isValid`, `isHomebrew`, `isTwlm`, `customIcon`, `bnrSysSettings`, `bnriconframenumY`, `bannerFlip`, `cachedTitle`, etc. Preenchidos por `getFileInfo`→`getGameInfo` (metadados) na carga da página.

## 3. Geometria do carrossel e centralização

Constantes em `graphics.cpp`: `titleboxXspacing = 58` (passo horizontal entre itens), `titleboxYpos = 85`.

- Cada item `pos` é desenhado em **`iconXpos = 112 + pos*titleboxXspacing`** (box em `96 + pos*spacing`), deslocado por **`titleboxXpos[secondaryDevice]`** (posição de scroll animada).
- O item **selecionado fica sempre centralizado**: o scroll `titleboxXpos` desliza pra pôr o `CURPOS` no centro.
- **`realCurPos = (titleboxXpos + 32) / titleboxXspacing`**: item atualmente centralizado (derivado do scroll). O render usa `realCurPos` pra efeitos de "leque"/aproximação dos vizinhos.
- O render desenha só a janela `pos = CURPOS-3 … CURPOS+3` (`maxIconNumber = 3`; Saturn = 0).

## 4. Cursor / indicadores de seleção

- Não há um "sprite de cursor" que se move; o **item selecionado é o do centro** (posição fixa na tela), e o carrossel rola por baixo.
- **Brace** (`tex().braceImage()`): colchetes desenhados nas bordas do box central (esquerda em ~`66-titleboxXpos`, direita espelhada) — moldura de seleção do tema DSi.
- **Bubble** (`drawBubble(tex().bubbleImage())`, `currentBg==1`): "balão" de fundo do item selecionado.
- **Dbox / info** (`drawDbox`): caixa com nome/desenvolvedora do jogo selecionado + box art.
- Como tudo é centralizado, o `CURPOS` "some" visualmente no centro — o feedback de seleção é o item central + brace + dbox, não uma caixa que percorre a lista.

## 5. Navegação e input (loop principal `browseForFile`)

Loop de espera: `do { scanKeys(); pressed=keysDown(); held=keysDownRepeat(); … updateText(false); } while(!held);`
A cada frame atualiza a info do item central (`titleUpdate(CURPOS)` quando `!bannerTextShown`) e roda checagens de compat (`checkDsiBinaries`, `checkRomAP`) num timer de 30 frames.

| Tecla | Ação |
|-------|------|
| `KEY_LEFT`/`KEY_RIGHT` (ou setas touch no tema DSi) | `moveCursor(false/true)` → CURPOS ∓/± 1 |
| `KEY_UP` (só `sortMethod==4`) | modo "mover app" (reordenar) |
| `KEY_L`/`KEY_R` | `previousPage`/`nextPage` (PAGENUM ∓1, CURPOS=0, recarrega dir/ícones) |
| `KEY_A`/`START` | lança o item central (requer `bannerTextShown && showSTARTborder`) |
| `KEY_Y` | `perGameSettings(...)` (config por-jogo) |
| `KEY_B` | sobe um diretório |
| Touch na scrollbar / arrastar | rola o carrossel; `CURPOS` re-sincronizado de `titleboxXpos` |

### `moveCursor(right, dirContents, maxEntry)`
`do { CURPOS±1; titleUpdate(CURPOS); iconUpdate(CURPOS±2) [carrega o ícone que entra]; anima titleboxXdest em 8 passos de titleboxXspacing/8; } while (tecla ainda held);`
- Ao fim de cada passo: `titleboxXdest = CURPOS*titleboxXspacing`.
- Nos limites (`CURPOS<=0` ou `>=last_used_box`): som de "edge bump", sem mover.
- A **animação real** de `titleboxXpos → titleboxXdest` acontece no `frameRateHandler` (IRQ de VCOUNT), não em `moveCursor`.

## 6. Sistema de ícones

- **6 bancos de textura** (`NDS_ICON_BANK_COUNT = 7`; banco 6 = moving app). **`banco = num % 6`** (`iconTitle.cpp`: `getIcon`, `iconUpdate`).
- **Carregamento sob demanda**: só ~5–6 ícones ao redor do `CURPOS` ficam residentes. Ao rolar, `moveCursor` chama `iconUpdate(CURPOS±2)` pra carregar o ícone que entra na janela; na carga da página, um laço carrega a janela inicial.
- `iconUpdate(isDir, name, num)` decodifica o banner e carrega no banco `num%6` (pastas → `clearIcon`). `drawIcon(x, y, num)` desenha o ícone 32×32 do banco com o frame de animação atual.
- **Metadados** (`getFileInfo`→`getGameInfo`) são carregados pra **todos os 40 itens** da página (não é lazy) — por isso os flags de boot (`isDSiWare`, `unitCode`, `bnrRomType`…) estão sempre válidos para qualquer `CURPOS`.

## 7. Título / texto

- **`titleUpdate(isDir, name, num)`** (`iconTitle.cpp`): só escreve o texto (nome/desenvolvedora) via `writeBannerText`/`writeDialogTitle`, usando `cachedTitle[num]`/`infoFound[num]`. **Não** carrega metadados de boot.
- **`updateText(top)`** (`fontHandler.cpp`): comita a fila de texto na camada de fonte (BG2 = baixo, BG6 = cima).
- Box art: carregada pro `CURPOS` de `_nds/TWiLightMenu/boxart/<TID>.png`.

## 8. Render (`vBlankHandler`, IRQ de VBLANK)

`if (updateFrame) { glBegin2D(); … glEnd2D(); GFX_FLUSH=0; }` — desenha a tela de baixo (3D). Ordem: wallpaper/chrome (bips/scrollwindow/brace), loop de ícones do carrossel (`pos = CURPOS±3`), moving app, dbox, box art, borda START.
- **`frameRateHandler`** (IRQ de VCOUNT): anima `titleboxXpos→titleboxXdest`, faz o **fade** (`screenBrightness` via `SetBrightness`, `fadeType`), e sinaliza `updateFrame`.
- **Fade**: `screenFadedIn()` = brightness==0; `screenFadedOut()` = brightness>24. O launch espera esses estados (`while(!screenFaded…())`).
- `bottomBgRefresh()` (fim do vblank) redesenha o BG da tela de baixo todo frame.

## 9. Backgrounds (agora cor sólida)

- `drawTopBg()` (SUB, buffer `_bgSubBuffer`) e `drawBottomBg(index)` (MAIN, `_bgMainBuffer`) — em vez de `_backgroundTextures[i].copy(...)`, preenchem o buffer com **`SOLID_BG_COLOR`** (`RGB15(20,24,31)|BIT(15)`, defina em `ThemeTextures.cpp`).
- `clearTopScreen()` preenche a tela de cima de branco (usado no launch).
- `bottomBgLoad(int, init)` escolhe a variante (1 base / 2 selecionado / 3 mover-app) e chama `drawBottomBg`.

## 10. Costuras de boot e config (mantidas)

- **Boot**: `browseForFile()` retorna o `entry->name` e seta `applaunch=true` (KEY_A). O **boot real fica em `main.cpp`** após o retorno (`if (applaunch)`), via `runNdsFile`/nds-bootstrap (soft-reset "passme" → carrega `nds-bootstrap-*.nds` → jogo). Decide DS/DSi mode pelos flags por-item.
- **Config**: `perGameSettings(name, …)` (KEY_Y) e o menu de settings.
- Lista de diretório: `getDirectoryContents`, `getFileInfo`, `DirEntry`.

## 11. Pontos de atenção (para o rework)

- O acoplamento **CURPOS (macro) × titleboxXpos (scroll)**: a seleção "centralizada" é derivada do scroll (`realCurPos`), enquanto launch/config usam `CURPOS`. Ao trocar o layout (ex.: grid/lista), garanta que o índice de seleção usado no desenho seja **o mesmo** que o launch (`CURPOS`), lido no **mesmo contexto** (o render roda no IRQ; publicar um snapshot do loop principal evita divergência).
- Só ~6 ícones residentes por vez (banco `num%6`): mostrar mais ícones simultâneos exige aumentar bancos e carregar a janela correspondente. **Desatualizado** — ver §12: hoje são `iconActiveBankCount()` bancos (padrão 24), dimensionado a partir da grade do tema.
- `getFileInfo` carrega metadados de toda a página → boot não depende de lazy-load do ícone.

## 12. Rework em andamento: componentes + layout.json (tema DSi, grid apenas)

Esta seção documenta o estado **pós-rework** do grid 3-linhas do tema DSi (§§3-4 acima já
descreviam o carrossel de 1 linha usado pelos *outros* temas — `else` branch de `vBlankHandler`,
inalterada). Fora de escopo: `romsel_r4theme`/`romsel_aktheme` (codebases próprias, sem grid).

**Arquitetura de componentes** (`arm9/source/graphics/components/`): o antigo bloco inline de
~60 linhas em `vBlankHandler` (`if (ms().theme == EThemeDSi) { ... }`) foi extraído em unidades
pequenas com split update/draw, no espírito "React-like" pedido pelo rework (não é virtual-DOM —
nesse hardware isso seria overhead sem benefício; o valor é composição + um único ponto de leitura
de config, não reconciliação):
- `Component.h` — interface mínima (`update()`→bool "precisa redesenhar", `draw()`).
- `GridView` — **o grid inteiro como um único componente**: geometria, animação de zoom da seleção
  e scroll horizontal, tudo em um objeto só, com estado próprio (`_scrollPos`/`_scrollDest`,
  **não** `titleboxXpos`/`titleboxXdest` — ver "Reconstrução do grid" abaixo para o porquê).
  `vBlankHandler` chama `gridView().update()` (cedo, antes do gate de redraw) e
  `gridView().draw()` (dentro do gate). Substitui o antigo trio
  `GridLayoutComponent`/`GridSelectionComponent`/`GridRenderer` (removidos — eram uma divisão
  prematura antes de existir um único dono do estado de scroll do grid).
- `GridItemComponent` — desenho de uma célula (box/folder + ícone), dado posição+escala já
  resolvidos; sem dependência de estado de seleção/scroll.
- `GridCursorComponent` — destaque opcional ao redor da célula selecionada (ver abaixo).

**`ThemeLayout` / `layout.json`** (`graphics/ThemeLayout.h`, contraparte em JSON do `theme.ini` /
`ThemeConfig`, só que para geometria de grid/assets/animações — `theme.ini` nunca teve essas
chaves): lido de `<pasta-do-tema>/layout.json` (`TFN_THEME_LAYOUT`) via `jsmn` (vendorizado em
`universal/include|source/common/jsmn.h|.c` — só parser tokenizado, sem DOM, sem heap por config).
Ausente/inválido → mantém os defaults do construtor (idênticos às constantes antigas). Schema:
`grid` (rows, colSpacing, rowSpacing, scales, zoomStep, scrollSpeed, cursor.enabled/asset),
`assets` (overrides de caminho por nome), `sprites` (tabelas nomeadas `{frameCount,
frameDelayVBlanks}` — `GridCursorComponent` já consome uma chamada `"gridCursorBlink"` como prova
de conceito do pipeline JSON→animação).

**"True grid"**: `NROWS` deixou de ser uma constante (`3`) e virou `tl().gridRows()`, com o pool de
bancos de ícone (`graphics/iconHandler.h`) redimensionado para `iconActiveBankCount() =
gridRows() * colunas-visíveis`, limitado a `NDS_ICON_MAX_BANKS` (orçamento de VRAM, banco A/128KB)
com aviso via log se um tema pedir mais que isso. **Limitação conhecida e deliberada**: todas as
`gridRows()` linhas de uma coluna são sempre desenhadas (sem scroll vertical independente quando
`gridRows() > `o que cabe na tela) — `num` (índice do item) hoje serve tanto de endereço físico de
banco (`num % iconActiveBankCount()`) quanto de índice lógico nos arrays `[40]/[41]` de
`ndsheaderbanner.h` (`bnriconframenumY`, `bannerFlip`, `customIcon`, etc., em `iconTitle.cpp`).
Rolagem vertical independente exigiria desacoplar esses dois usos — não implementado neste passo
por ser uma mudança de maior risco em código que toca VRAM diretamente, sem hardware disponível
para validar. Fica como próximo passo natural caso um tema realmente precise de mais linhas do que
cabem na tela de uma vez.

## 13. Reconstrução do grid: por que `GridView` tem seu próprio scroll

O grid nunca teve um "dono" único do seu estado de scroll horizontal — ele reaproveitava
`titleboxXpos`/`titleboxXdest` (`graphics.cpp`), as mesmas variáveis do carrossel de 1 linha que
ele substituiu para o tema DSi. Como esses arrays são globais e `fileBrowse.cpp` tem ~4600 linhas
de código de input acumulado ao longo dos anos, sobrou uma dúzia de pontos que escrevem neles
usando a fórmula do carrossel (`CURPOS * titleboxXspacing`, um item por passo) sem checar o tema —
corretos para o carrossel, errados para o grid (que precisa de `(CURPOS/rows) * colSpacing`, uma
*coluna* por passo). Cada um desses pontos "funciona" isoladamente (não trava nada), mas deixa o
scroll do grid num valor incorreto até a próxima navegação D-pad recalcular certo — na prática,
"o grid herda comportamento do carrossel antigo" bem literalmente. Os mais relevantes encontrados
e corrigidos:

- **`graphicsInit()`** (`graphics.cpp`) — inicializava `titleboxXpos`/`titleboxXdest` de *ambas* as
  telas a partir da posição do cursor salva, com a fórmula do carrossel, incondicionalmente. Se o
  cursor salvo não fosse 0 (qualquer sessão que não seja a primeira), o grid nascia posicionado
  errado no boot, só se corrigindo (com um salto visível) na primeira navegação. Corrigido com
  `gridView().jumpToItem(...)` logo em seguida.
- **`nextPage()`** (`fileBrowse.cpp`) — ao ir para a última página e pousar num item que não é o
  0, setava o destino do scroll com a fórmula do carrossel. Alcançável no grid via SELECT+direita
  (atalho de troca de página; L/R diretos já são explicitamente desabilitados para o tema DSi).
- **Clamp de `CURPOS` no início de `browseForFile()`** — idem, incondicional.
- **Loop principal** (`do { ... } while (!held)`) — já recalculava o destino do scroll do grid a
  cada frame (correto), só precisou passar a escrever em `gridView()` em vez de `titleboxXdest`.
- **Modo "mover app" (arrastar para reordenar, `KEY_UP` com `sortMethod==4`)** — a implementação
  inteira (~160 linhas) nunca foi adaptada para o grid: usa `moveCursor()` (a navegação de item
  único do carrossel) e a fórmula de scroll do carrossel para tudo. Reimplementar isso
  corretamente para um grid 2D (arrastar um item entre linhas *e* colunas) é uma feature nova, não
  um bugfix — em vez de deixar o código antigo corromper o estado do grid, a entrada no modo foi
  **desabilitada para o tema DSi** (`ms().theme != TWLSettings::EThemeDSi` na condição de entrada);
  os outros temas continuam com a feature normalmente. Reordenar itens no grid fica como trabalho
  futuro que precisa de design próprio, não de portar a lógica do carrossel.
- **Gestos de toque "swipe tap"/"drag scroll"** (`touch.py` entre 76 e 164 no tema DSi) — na
  leitura do código pareciam alcançáveis para o grid, mas na verdade **nunca são**: o branch do
  "Grid tap" (`touch.py < 164`) é verificado antes na mesma cadeia `if/else if` e sua condição é um
  superconjunto da condição desses gestos para o tema DSi, então sempre intercepta primeiro. Código
  morto para o DSi (ainda vivo e correto para os outros temas) — não foi tocado.

Fora esses pontos de estado compartilhado, a animação de zoom da seleção também carregava um bug
menor herdado do carrossel: o sentinela inicial `gridSelCur = -1` fazia o item já selecionado
"crescer" com uma animação de zoom-in na primeira vez que o grid aparecia (o carrossel usava esse
mesmo sentinela para "nada selecionado ainda", mas como só existe *uma* posição em destaque no
carrossel, o efeito colateral nunca incomodou lá). `GridView` agora inicializa `_selCur` já na
posição do cursor persistida, então o item inicialmente selecionado nunca reproduz essa animação
fantasma.

## 14. Efeito de lançamento do jogo: `LaunchWipeComponent`

O efeito tocado entre apertar START num jogo e o nds-bootstrap assumir (`applaunchprep`) era o
ícone "flutuando pra cima e saindo da tela" (`titleboxYmovepos`, incrementado em `vBlankHandler` e
lido só nesse trecho de desenho) — universal a todos os temas, não específico do grid. Substituído
por `graphics/components/LaunchWipeComponent`: um círculo escuro que nasce em cima do ícone
selecionado no grid, na tela de baixo, e expande até engolir as duas telas.

As duas telas físicas são framebuffers separados sem espaço de coordenadas em comum, mas o efeito
precisa parecer *um* círculo só vazando da tela de baixo pra cima — então os dois métodos de
desenho medem distância no mesmo espaço combinado imaginário: a tela de cima fica "acima" da tela
de baixo, deslocada por `SCREEN_GAP` (um substituto pro bezel físico). A origem e o raio-alvo são
capturados uma vez por lançamento (`captureOrigin()`, na borda de subida de `applaunchprep`), não a
cada frame — a origem vem de `gridView().columnCenterX/rowCenterY` na posição do item selecionado
(fallback pro centro da tela nos outros temas, que não têm grid), e o raio-alvo é a distância até o
canto mais longe do espaço combinado a partir dali, dividida por `TOTAL_FRAMES` pra manter a mesma
duração (~0.66s) não importa onde o ícone esteja.

Como as duas telas têm pipelines de desenho diferentes (ver §1), o efeito precisa de dois caminhos:
- **Tela de baixo**: os itens do grid/carrossel são sprites gl2d, então o wipe também precisa ser
  geometria gl2d pra desenhar por cima deles — um leque de triângulos (`glTriangleFilled`)
  aproximando um círculo preenchido a partir de uma tabela de 20 pontos de um círculo unitário em
  ponto fixo (sem trig em tempo de execução — o ARM9 do DS não tem FPU). Chamado de dentro do
  `glBegin2D()/glEnd2D()`, no lugar do antigo desenho do ícone.
- **Tela de cima**: não tem camada gl2d (§1), é um buffer de pixels puro (`BG_GFX_SUB`), então o
  wipe ali é um preenchimento por scanline (por linha, calcula a meia-largura via `sqrtf` e pinta
  o intervalo de preto). Chamado fora do `glBegin2D()/glEnd2D()`, direto em `vBlankHandler`.

`update()` cresce o raio enquanto `applaunchprep` for verdadeiro e reseta quando ele cai (próximo
lançamento recomeça o wipe do zero). Sem estado compartilhado com o carrossel — nem
`titleboxYmovepos` nem qualquer variável do bloco antigo sobreviveram.

## 15. HUD da tela superior: bateria, relógio, título/prompt e logo do jogo

`ThemeTextures.cpp` concentrava, como estado estático solto no arquivo, tudo que aparece na tela
superior por cima do fundo (brick/vídeo): a caixa de título (+ texto do jogo selecionado), a caixa
de prompt "aperte start" que a substitui no ócio, a barra de status (fundo + hora + bateria) e o
logo do jogo (decode adiado + zoom). Cinco componentes novos em
`graphics/components/` assumiram cada um, seguindo o mesmo padrão `compose(u16 *dst)` do restante
do HUD do topo (ver §1/§8 — a tela de cima é um buffer de pixels só, não gl2d, então os componentes
que vivem nela escrevem em `dst` em vez de ter um `draw()` sem parâmetros):

- **`TopScreenBoxBmp`** — não é um componente, é o loader de BMP compartilhado que
  `GameTitleComponent` e `StatusBarComponent` usam pra carregar suas caixas (`topscreen_titlebox.bmp`,
  `topscreen_startbox.bmp`, `status_bar.bmp`): decodifica 4/8bpp com magenta transparente e localiza
  a caixa opaca dentro da tela cheia (os temas desenham a caixa dentro de um canvas do tamanho da
  tela, pra poder posicioná-la livremente sem que as dimensões do `.bmp` amarrem o layout).
- **`GameLogoComponent`** — decodifica o `logo.png` do jogo (lodepng, custoso) só depois que a
  seleção fica parada por `LOAD_DELAY` frames (o mesmo debounce de antes), com zoom-in ao aparecer e
  zoom-out mais rápido ao trocar de item (pra não "virar" o logo novo no meio da animação). Composto
  com sombra (offset + alphablend) antes da caixa de título, então fica atrás dela.
- **`GameTitleComponent`** — um componente só pra caixa de título+texto E a caixa de prompt, porque
  são duas faces mutuamente exclusivas de um único slot animado (troca por slide vertical, dirigida
  por um timer de ócio que reseta a cada seleção) — nunca as duas ao mesmo tempo, então não fazia
  sentido serem componentes separados.
- **`BatteryComponent`** — ícone de bateria (5 níveis + carregando, PNGs), ancorado a uma posição
  FIXA dentro da barra (a `ClockComponent`/hora que se ajusta em torno dele, não o contrário).
  `compose()` devolve a borda esquerda do ícone, que `StatusBarComponent` repassa pro relógio.
- **`ClockComponent`** — hora renderizada com a fonte pequena, reduzida por OR-downsample (um pixel
  de destino acende se QUALQUER pixel-fonte na sua célula mapeada estiver aceso — preserva traços
  finos da fonte melhor que média), alinhada à direita crescendo pra esquerda a partir da borda que
  `BatteryComponent` devolveu.
- **`StatusBarComponent`** — orquestra o fundo da barra + `BatteryComponent` + `ClockComponent`, e
  guarda o cache de "mudou desde o último compose" que o loop ocioso usa (`tickStatusBar()`) pra só
  recompor o topo quando a hora ou o nível de bateria realmente mudam — sem isso, o relógio pisca a
  cada frame no hardware (mesmo motivo do "compõe uma vez, blita uma vez" do §1).

`ThemeTextures::drawTopTitle()` ficou só com o essencial que continua genuinamente seu: montar o
fundo (brick sólido / vídeo / fade entre os dois — inalterado) e então chamar, nessa ordem,
`gameLogo().compose(dst)` → `gameTitle().compose(dst, text)` → `statusBar().compose(dst)`, antes do
`tonccpy` final pro `BG_GFX_SUB`. `loadGameLogo()` continua dono de decidir *qual* arquivo carregar
(a resolução de asset é compartilhada com o vídeo de gameplay por jogo, que não mudou de lugar) mas
delega o *quando decodificar* pra `gameLogo().scheduleDecode()`.

Um nome exigiu cuidado: o free function do relógio não pôde se chamar `clock()` — colide com
`clock_t clock(void)` de `<time.h>`, incluído transitivamente por quase tudo. Chama-se `hudClock()`.

`getBatteryLevel()` (em `ThemeTextures`) precisou virar `public` — antes só era chamado de dentro da
própria classe, mas `BatteryComponent`/`StatusBarComponent` agora leem o nível de bateria de fora.

Duas novas chaves em `theme.ini` (`ThemeConfig`, mesmo padrão de `BatteryRenderX/Y` etc. — não
`layout.json`/`ThemeLayout`, que é só do grid): `StatusBarContentOffsetX`/`StatusBarContentOffsetY`,
lidas por `tc().statusBarContentOffsetX()/Y()`. Deslocam a bateria+hora JUNTAS dentro do
`status_bar.bmp` (`StatusBarComponent::compose()` soma o offset a `barX`/`barY` antes de chamar
`battery().compose()`/`hudClock().compose()`) — não afetam o layout relativo entre elas (bateria
ancorada à direita, hora à esquerda dela), só onde esse par inteiro cai dentro da barra. Default 0/0
(sem mudança de comportamento pra temas que não definem a chave, fail-open como o resto do
`ThemeConfig`).

Mais três chaves, agora pro logo do jogo (`GameLogoComponent`): `LogoZoomPercent` (100 = tamanho
normal; multiplica `_logoW/_logoH` por cima da animação de zoom-in/out da seleção — a animação
continua indo de 0 até ESTE tamanho-alvo, não até 100%) e `LogoOffsetX`/`LogoOffsetY` (px, a partir
do centro da tela superior, podem ser negativos). Um detalhe que só apareceu ao permitir offset
negativo: o loop principal do blit do logo saía do loop inteiro com `break` assim que uma linha
caísse fora da tela, o que era inofensivo antes (a posição Y sempre começava com o clamp `ly>=0` já
aplicado, então `py` só crescia) mas com `LogoOffsetY` negativo `py` pode COMEÇAR negativo e voltar a
ficar válido em linhas seguintes — trocado pra `continue`.

## 16. Debug menu (`DSI_DEBUG_MENU`): fps, custo de render, RAM/VRAM e captura de log

`ThemeTextures::drawTopDebug()` (já existia como um overlay de 3 linhas: fps, polígonos/vértices,
VRAM dos bancos de ícone) virou um painel de 10 linhas: fps + `vblankWorkPercent()` (custo da PRÓPRIA
`vBlankHandler()` como % do orçamento de ~16.7ms/frame — medido com `cpuStartTiming(2)`/
`cpuEndTiming()`, timers de hardware 2+3 cascateados, não usados em mais lugar nenhum do código),
heap (`mallinfo()` — só alocações via malloc/new, não vê os buffers estáticos abaixo), VRAM dos
bancos de ícone, e os top-3 maiores consumidores de RAM e de VRAM entre um conjunto fixo de
candidatos conhecidos (buffers de composição do topo, vídeo de gameplay, os 4 componentes do HUD,
bancos de ícone, e as maiores texturas paletizadas de UI). `gatherDebugLines()` centraliza a coleta
pra ela nunca divergir entre o overlay em tela e a captura em arquivo abaixo.

**Segurar L+R** com o debug menu ativo por ~1s grava esse mesmo snapshot em
`_nds/TWiLightMenu/dsimenu_perf.log` (append, uma captura por chamada) — pensado pra comparar
antes/depois ao investigar consumo, sem precisar fotografar a tela.

Dois bugs de performance apareceram construindo isso, ambos na própria instrumentação:
- `cpuStartTiming`/`cpuEndTiming` rodavam **todo frame, incondicionalmente** (mesmo com o debug menu
  desligado) -- reconfigurar timer de hardware dentro da IRQ de vblank sem ninguém olhar o número.
  Agora só rodam se `ms().dsiDebugMenu` estiver ligado.
- O overlay maior (10 linhas x 132px vs. as 3 x 92px de antes, ~4x mais pixels) continuava sendo
  redesenhado a 60Hz, pixel-a-pixel, direto no VRAM da tela superior (`BG_GFX_SUB[...] = ...`, sem
  DMA/batch) -- isso sozinho bastava pra estourar o orçamento de um frame e perder um vblank a cada
  dois (framerate pela metade). O contador de fps (`tickFpsCounter()`) continua rodando todo frame,
  mas o desenho pesado em si (`gatherDebugLines()` + blit) foi throttled pra 1 a cada 6 frames
  (~10Hz) -- suficiente pra um painel de debug.

O próprio ranking de RAM revelou dois desperdícios reais -- `GameTitleComponent`/`StatusBarComponent`
guardavam um buffer do tamanho máximo possível (256×192 ou 256×64) pra sempre, mesmo o tema padrão
usando assets já pequenos (titlebox 256×47, startbox 95×25, status bar 80×27); `_menuBgDither`
(overlay dithered sobre vídeo, 96KB) era carregado mesmo com `DSI_VIDEO_BG` desligado -- e uma
primeira tentativa de corrigir os dois **piorou o desempenho do app inteiro**, não só desses
componentes. Vale registrar por quê:

A primeira versão trocou os buffers estáticos desses 3 assets por alocação em heap (`new
u16[bw*bh]`, exatamente o tamanho do recorte, `TopScreenBoxBmp` cropando pro bounding box opaco em
vez de manter o canvas inteiro) -- no papel, um baita corte de RAM (256×192 -> ~16KB pro titlebox
real, por exemplo). Na prática, o debug menu recém-construído (ver acima) mostrou o problema:
capturado no hardware real, `Heap 1429K/1445K` -- só 16KB livres dentro do que já havia sido
reservado do sistema, heap praticamente saturado. Mover esses buffers de `.bss` (estático, custo
zero de alocação, invisível pro `mallinfo()`) pra dentro desse heap já apertado -- e ainda com um
padrão de alocação ruim (`new[]` pro canvas completo + `new[]` pro recorte + `delete[]` do primeiro,
por asset) -- foi o suficiente pra pressionar/fragmentar um heap que já não tinha folga, derrubando
o desempenho de QUALQUER alocação no app inteiro (por isso o sintoma batia em boot, navegação e HUD
ao mesmo tempo, mesmo com o debug menu desligado -- o heap é compartilhado por todo o app, não é
algo isolado a essas 3 telas).

**Revertido** para buffers estáticos (estático de propósito, ver o comentário em
`TopScreenBoxBmp.h`), só que menores que o `256×192`/`256×192` originais: `GameTitleComponent` agora
usa `MAX_H=64` (era 192) pro titlebox e startbox -- generoso o bastante acima do asset real (47px)
sem chegar a reservar a tela inteira, e sem tocar heap nenhum. `_menuBgDither` voltou a ser
incondicional (mesmo trade-off de RAM-vs-estabilidade). O ganho de RAM final é bem mais modesto que
a versão em heap (titlebox+startbox: 192KB -> 64KB, não ~20KB), mas com ZERO risco de contenção de
alocador -- a lição geral: **num heap já sob pressão, reduzir bytes por alocação vale bem menos do
que reduzir o NÚMERO de alocações**, e mover algo de `.bss` pro heap troca RAM "grátis" por RAM
contestada, mesmo que o total de bytes caia.

`ramFootprintBytes()` dos 4 componentes do HUD voltou a ser `static constexpr` (de novo `sizeof()`
de um array fixo, conhecido em tempo de compilação) -- não mais um método de instância.

## 17. `_backgroundTextures`: dois assets carregados mas nunca lidos, pro tema grid

Investigando "quanto de VRAM o background consome" (resposta: 0 -- ele vive em RAM, não em textura
de VRAM, ver `_menuBgBuffer`/`_backgroundTextures` no §16), apareceu um achado à parte: `loadBackgrounds()`
carrega 4 imagens de fundo pro tema DSi (`0:top, 1:bottom, 2:bottom_bubble, 3:bottom_moving`, todas
256×192 = 96KB cada como `Texture` em heap), mas um grep de todo `_backgroundTextures[N]` neste
arquivo mostra que só os índices `0`, `1` e `ms().macroMode` (0 ou 1 -- os mesmos dois) são lidos em
algum lugar. Índices `2` (bubble) e `3` (moving) nunca são indexados por ninguém.

Faz sentido: o afordance de seleção do `GridView` é um zoom-scale no próprio ícone (§12), não uma
troca de fundo ao passar por cima; e "mover apps" (drag-to-reorder, que usaria o fundo "moving") está
explicitamente desligado pro tema DSi (`fileBrowse.cpp`, `ms().theme != TWLSettings::EThemeDSi` na
condição de entrada -- a implementação antiga era 100% carrossel e nunca foi adaptada pro grid 2D).

`loadBackgrounds()` agora pula `bottom_bubble`/`bottom_bubble_macro` especificamente quando
`ms().theme == TWLSettings::EThemeDSi` (a emissão de `bottom_moving`/`_macro` já era condicionada
só a DSi, então só removi essas duas linhas por completo). ~192KB de heap + 2 leituras de arquivo no
boot a menos. HB/Saturn (que caem no mesmo bloco de código, sem retornar cedo como o 3DS) continuam
carregando `bubble` normalmente -- não auditei o uso deles nesses temas, então não mexi.

Nota sobre verificação: o log do melonDS (`Opened ".../bottom_bubble.png"`) NÃO serve pra confirmar
se esse carregamento específico rodou ou não -- ele aparece pra QUALQUER tema do cartão (DiiSU Dark,
DS Menu V2, etc.), porque é o FolderSync do DLDI (`preview.sh`) sincronizando a pasta inteira do SD
pro disco virtual do lado do host, não uma chamada `fopen()` do nosso código guest. Só dá pra
confirmar de verdade lendo o código (feito) ou comparando o número de heap do debug menu antes/depois
no hardware real.
