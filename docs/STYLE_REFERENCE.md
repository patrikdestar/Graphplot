# Style reference

Every visual setting is a style key. Use them in `.gpstyle` files (`key: value`),
in *Style → Edit style…*, or from Python (`plotter.style.set({...})`, `fig.set_style({...})`).

Built-in styles: `default`, `science`, `nmr`, `presentation`. A style file may start with `inherit: <built-in or file>`.

| Key | Default | science | Description |
|---|---|---|---|
| `font.family` | `sans-serif` | `Helvetica, Arial, Nimbus Sans, FreeSans, Liberation Sans, sans-serif` | font preference list; generic names: sans-serif, serif, monospace |
| `font.size` | `10` | `12` | base font size (pt) |
| `figure.facecolor` | `#ffffff` | `#ffffff` | background outside the axes |
| `figure.padding` | `6` | `14` | empty border around the figure (px) |
| `axes.facecolor` | `#ffffff` | `#ffffff` | background inside the axes |
| `axes.edgecolor` | `#52514e` | `#000000` | frame and tick colour |
| `axes.linewidth` | `1` | `1.2` | frame line width (px) |
| `axes.labelsize` | `10` | `14` | axis label font size (pt) |
| `axes.labelcolor` | `#0b0b0b` | `#000000` | axis label colour |
| `axes.labelpad` | `4` | `6` | space between tick labels and axis label (px) |
| `axes.titlesize` | `11` | `14` | title font size (pt) |
| `axes.titlebold` | `true` | `true` | bold title |
| `axes.titlecolor` | `#0b0b0b` | `#000000` | title colour |
| `axes.unicode_minus` | `true` | `true` | use a real minus sign in tick labels |
| `axes.formatter` | `auto` | `sci` | tick label number format (auto \| plain \| sci) |
| `text.math` | `true` | `true` | interpret $math$, **bold** and *italic* in texts |
| `xtick.direction` | `in` | `in` | tick direction (in \| out \| inout) |
| `xtick.major.size` | `5` | `10` | major tick length (px) |
| `xtick.minor.size` | `3` | `5` | minor tick length (px) |
| `xtick.width` | `1` | `1.2` | tick line width (px) |
| `xtick.minor.visible` | `false` | `true` | show minor ticks |
| `xtick.top` | `true` | `true` | ticks also on the top side |
| `xtick.labelsize` | `9` | `12` | tick label font size (pt) |
| `xtick.labelcolor` | `#52514e` | `#000000` | tick label colour |
| `xtick.count` | `5` | `5` | approximate number of major ticks |
| `ytick.direction` | `in` | `in` | tick direction (in \| out \| inout) |
| `ytick.major.size` | `5` | `10` | major tick length (px) |
| `ytick.minor.size` | `3` | `5` | minor tick length (px) |
| `ytick.width` | `1` | `1.2` | tick line width (px) |
| `ytick.minor.visible` | `false` | `true` | show minor ticks |
| `ytick.right` | `true` | `true` | ticks also on the right side |
| `ytick.labelsize` | `9` | `12` | tick label font size (pt) |
| `ytick.labelcolor` | `#52514e` | `#000000` | tick label colour |
| `ytick.count` | `5` | `5` | approximate number of major ticks |
| `axes.grid` | `true` | `false` | show the major grid |
| `grid.color` | `#e4e3de` | `#e4e3de` | grid colour |
| `grid.linewidth` | `1` | `1` | grid line width (px) |
| `grid.linestyle` | `-` | `-` | grid line style (- \| -- \| : \| -.) |
| `grid.minor` | `false` | `false` | show the minor grid |
| `grid.minor.color` | `#f0efec` | `#f0efec` | minor grid colour |
| `axes.prop_cycle` | `#2a78d6, #eb6834, #1baf7a, #eda100, #e87ba4, #008300, #6250d6, #e34948` | `#5790fc, #f89c20, #e42536, #964a8b, #9c9ca1, #7a21dd` | comma-separated colour cycle for new datasets |
| `lines.linewidth` | `1.5` | `2` | default line width (px) |
| `lines.markersize` | `6` | `7` | default symbol size (px) |
| `errorbar.capsize` | `3` | `0` | error bar cap width (px) |
| `legend.frameon` | `true` | `false` | draw a frame around the legend |
| `legend.loc` | `upper right` | `upper right` | legend position (upper right \| upper left \| lower right \| lower left \| upper center \| lower center \| center left \| center right \| center) |
| `legend.fontsize` | `9` | `12` | legend font size (pt) |
| `legend.facecolor` | `#e6ffffff` | `#e6ffffff` | legend background |
| `legend.edgecolor` | `#e4e3de` | `#e4e3de` | legend frame colour |
| `label.top.left` | `` | `` | text above the frame, left (e.g. **CMS** *Preliminary*) |
| `label.top.right` | `` | `` | text above the frame, right (e.g. 138 fb$^{-1}$ (13 TeV)) |
| `label.inside` | `` | `` | text inside the frame |
| `label.inside.loc` | `upper left` | `upper left` | position of label.inside (upper left \| upper right \| lower left \| lower right) |
| `label.inside.size` | `12` | `12` | font size of label.inside (pt) |
| `contour.linewidth` | `1` | `1` | contour line width (px) |
| `contour.negativecolor` | `#e34948` | `#e34948` | colour of negative contours |
| `contour.levels` | `12` | `12` | number of contour levels for new 2D data |
| `contour.factor` | `1.4` | `1.4` | ratio between successive contour levels |
| `peak.color` | `#0b0b0b` | `#000000` | peak cross colour |
| `peak.size` | `7` | `7` | peak cross half-size (px) |
| `peak.linewidth` | `1.2` | `1.2` | peak cross line width (px) |
| `peak.labelsize` | `9` | `11` | peak label font size (pt) |
| `peak.labelcolor` | `#0b0b0b` | `#0b0b0b` | peak label colour |
| `annotation.size` | `10` | `12` | default annotation font size (pt) |
| `annotation.color` | `#0b0b0b` | `#0b0b0b` | default annotation colour |
