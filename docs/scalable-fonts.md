# Scalable fonts on X4 Classic, X4 Pro, and Sticky

On the X4 Classic, X4 Pro, and Sticky, CrossInk's built-in fonts (Bitter and
Lexend Deca) are scalable, so you can pick any size from 8 to 22 pt. You can also
add your own static TrueType (`.ttf`) and OpenType (`.otf`) fonts. These
readers have extra memory that makes this possible. The original X3 and X4 use
fixed-size fonts and `.cpfont` font packs instead.

## Add your own scalable fonts

1. Copy your `.ttf` or `.otf` files into a family folder on the SD card, such as
   `/fonts/My Font/`. The hidden `/.fonts/My Font/` location works too.
2. If you copied the files using a computer, put the SD card back and restart the
   reader so it finds them.
3. Choose the family from **Settings > Reader > Font Options > Font Family**, or
   from the font picker while reading.

You can also upload `.ttf` or `.otf` files through the **Fonts** tab in the reader's web
interface. See [SD Card Fonts](./sd-card-fonts.md) for how to open that interface.
No font conversion or separate files for each size are needed.

> [!NOTE]
> A "static" font file holds one fixed style, such as Regular or Bold. These are
> supported. "Variable" fonts (one file that holds many weights) and font
> collections are not.

For the full range of text styles, add all four files for a family: **regular**,
**bold**, **italic**, and **bold italic**. CrossInk can use a family with fewer
styles, but missing styles may appear in another available style. If the files
are in a family folder, that folder's name appears in the font picker. Keep
scalable fonts and `.cpfont` files in separate family folders.

## Adjust the appearance

After selecting a custom `.ttf` or `.otf` family, open **TTF Rendering** under
**Settings > Reader > Font Options**, or from the in-book font menu. These
controls (such as **Hinting**, **Weight**, **Slant**, and **Stem Darkening**)
adjust how the letters look on the e-ink screen. Changes to a family apply to
all its styles and are remembered for that family. **Reset TTF Defaults** undoes
them. This option appears only for custom `.ttf` and `.otf` fonts; `.cpfont`
fonts do not use it.

Custom fonts use the same **8-22 pt** size choices as the built-in fonts. Changing fonts or
their appearance may make CrossInk lay out the current book again, so page
positions can change. Your bookmarks and clippings remain saved.

## Supported files and limits

- Use static TrueType `.ttf` or OpenType CFF `.otf` files. Some CFF fonts have a
  `.ttf` filename; CrossInk checks the file contents. Variable fonts and font
  collections are not supported.
- Each file must be **2 MiB or smaller**, and the four files in one family must
  total **6 MiB or less**.
- If both `/fonts` and `/.fonts` contain the same family, the copy in
  `/.fonts` takes precedence.

If a font does not appear, check its file type and size, then restart the
reader. If the reader has too little free memory to open a
font, it may use a fallback font instead.

## Implementation and upstream attribution (Advanced)

The `.otf` discovery and web upload support follows
[CrossPoint runtime SD fonts #3646](https://github.com/crosspoint-reader/crosspoint-reader/pull/3646)
and [web font upload #3717](https://github.com/crosspoint-reader/crosspoint-reader/pull/3717).
Upstream accepts the `.otf` extension but its FreeInk SDK has no CFF (OpenType
outline) driver, so extension support alone is not full OpenType support.

CrossInk's S3 builds enable the SDK's CFF driver (`FREEINK_FONT_ENABLE_CFF`), so
static OpenType CFF fonts really render, including CFF fonts named `.ttf`. The
existing PSRAM feature gates, style metadata, font memory arenas, and
content-based cache identities are kept. The font catalog version changed so old
discovery results are rebuilt. The **TTF Rendering** menu also controls
OpenType faces.
