# jwz iOS/macOS API reference for wayshade

Date: 2026-09-27

## Scope and provenance

This is a behavioral reference for a cross-platform wayshade decision. It does not import or derive source code into this repository.

The requested `https://github.com/Zawinski/xscreensaver` URL returned 404. The tree examined was the current public read-only mirror at `https://github.com/Zygo/xscreensaver`, cloned to `/home/jasonperlow/xscreensaver-ref`, commit `b99f6216cd23dbd6a9e1313f118fc08bfc30256d` (commit subject: `From https://www.jwz.org/xscreensaver/xscreensaver-6.16.tar.gz`). The mirror identifies itself as read-only in its repository description; the commit identifies the jwz.org 6.16 tarball as its source. All upstream line references below are to that commit.

The relevant boundary is explicit: `jwzgles.c` says it is an OpenGL 1.3 to OpenGL ES compatibility shim and names immediate-mode conversion, display lists, and matrix shadowing as its three main jobs (`jwxyz/jwzgles.c:17-26`). It targets GLES 1.1, not GLES 2 (`jwxyz/jwzgles.c:85-94`). wayshade instead links EGL and GLES directly (`src/gles3_compat.c:7-14`) and supplies shader-backed fixed-function behavior.

## Measured minimum GL1 API surface

Method: the count is the unique `jwzgles_gl*` declarations under the header's "things re-implemented in jwzgles.c" contract (`jwxyz/jwzglesI.h:160-166`, `jwxyz/jwzglesI.h:197-200`, `jwxyz/jwzglesI.h:383-393`). It excludes the six `glu*` helpers and the three state-management functions. It includes wrappers that intentionally no-op or assert, because they remain part of the accepted catalogue-facing API. The result is **166 GL entry points**. The implementation itself warns that it implements only what xscreensaver needed (`jwxyz/jwzgles.c:139-141`), and its selection and attribute-stack functions demonstrate why "entry point" does not mean complete semantics (`jwxyz/jwzgles.c:3015-3076`).

### Immediate mode and vertex arrays (57)

`glBegin`, `glColor3b`, `glColor3bv`, `glColor3d`, `glColor3dv`, `glColor3f`, `glColor3fv`, `glColor3i`, `glColor3iv`, `glColor3s`, `glColor3sv`, `glColor3ub`, `glColor3ubv`, `glColor3ui`, `glColor3uiv`, `glColor3us`, `glColor3usv`, `glColor4b`, `glColor4bv`, `glColor4d`, `glColor4dv`, `glColor4f`, `glColor4fv`, `glColor4i`, `glColor4iv`, `glColor4s`, `glColor4sv`, `glColor4ub`, `glColor4ubv`, `glColor4ui`, `glColor4uiv`, `glColor4us`, `glColor4usv`, `glColorPointer`, `glDisableClientState`, `glDrawArrays`, `glDrawElements`, `glEnableClientState`, `glEnd`, `glInterleavedArrays`, `glNormal3f`, `glNormal3fv`, `glNormalPointer`, `glRectf`, `glRecti`, `glVertex2dv`, `glVertex2f`, `glVertex2fv`, `glVertex2i`, `glVertex3dv`, `glVertex3f`, `glVertex3fv`, `glVertex3i`, `glVertex4f`, `glVertex4fv`, `glVertex4i`, `glVertexPointer` (`jwxyz/jwzglesI.h:167-194`, `jwxyz/jwzglesI.h:214-215`, `jwxyz/jwzglesI.h:240-271`, `jwxyz/jwzglesI.h:279-280`, `jwxyz/jwzglesI.h:338`, `jwxyz/jwzglesI.h:346-347`, `jwxyz/jwzglesI.h:388-391`).

### Matrix stack (11)

`glFrustum`, `glLoadIdentity`, `glMatrixMode`, `glMultMatrixf`, `glOrtho`, `glPopMatrix`, `glPushMatrix`, `glRotated`, `glRotatef`, `glScalef`, `glTranslatef` (`jwxyz/jwzglesI.h:221-228`, `jwxyz/jwzglesI.h:236-239`, `jwxyz/jwzglesI.h:296-299`).

### Display lists (6)

`glCallList`, `glDeleteLists`, `glEndList`, `glGenLists`, `glIsList`, `glNewList` (`jwxyz/jwzglesI.h:163-166`, `jwxyz/jwzglesI.h:188-189`, `jwxyz/jwzglesI.h:350`).

### Texture (25)

`glActiveTexture`, `glBindTexture`, `glCopyTexImage2D`, `glCopyTexSubImage2D`, `glDeleteTextures`, `glGenTextures`, `glPixelStorei`, `glTexCoord1f`, `glTexCoord2f`, `glTexCoord2fv`, `glTexCoord3f`, `glTexCoord3fv`, `glTexCoord4f`, `glTexCoord4fv`, `glTexCoordPointer`, `glTexEnvf`, `glTexEnvi`, `glTexGenfv`, `glTexGeni`, `glTexImage1D`, `glTexImage2D`, `glTexImage3D`, `glTexParameterf`, `glTexParameteri`, `glTexSubImage2D` (`jwxyz/jwzglesI.h:170-176`, `jwxyz/jwzglesI.h:201-202`, `jwxyz/jwzglesI.h:278`, `jwxyz/jwzglesI.h:295`, `jwxyz/jwzglesI.h:300-345`, `jwxyz/jwzglesI.h:360`, `jwxyz/jwzglesI.h:391`).

### Lighting and material (13)

`glColorMaterial`, `glLightModelf`, `glLightModelfv`, `glLightModeli`, `glLightModeliv`, `glLightf`, `glLightfv`, `glLighti`, `glLightiv`, `glMaterialf`, `glMaterialfv`, `glMateriali`, `glMaterialiv` (`jwxyz/jwzglesI.h:272-275`, `jwxyz/jwzglesI.h:287-294`, `jwxyz/jwzglesI.h:351`).

### Miscellaneous state, raster, query, and buffers (54)

`glAlphaFunc`, `glBindBuffer`, `glBitmap`, `glBlendColor`, `glBlendEquation`, `glBlendFunc`, `glBufferData`, `glClear`, `glClearColor`, `glClearDepth`, `glClearIndex`, `glClearStencil`, `glClipPlane`, `glColorMask`, `glCullFace`, `glDepthFunc`, `glDepthMask`, `glDisable`, `glDrawBuffer`, `glEnable`, `glFinish`, `glFlush`, `glFogf`, `glFogfv`, `glFogi`, `glFogiv`, `glFrontFace`, `glGetBooleanv`, `glGetDoublev`, `glGetFloatv`, `glGetIntegerv`, `glGetPointerv`, `glGetTexGenfv`, `glHint`, `glInitNames`, `glIsEnabled`, `glLineWidth`, `glLogicOp`, `glPointSize`, `glPolygonMode`, `glPolygonOffset`, `glPopAttrib`, `glPopName`, `glPushAttrib`, `glPushName`, `glRenderMode`, `glScissor`, `glSelectBuffer`, `glShadeModel`, `glStencilFunc`, `glStencilMask`, `glStencilOp`, `glUseProgram`, `glViewport` (`jwxyz/jwzglesI.h:190-194`, `jwxyz/jwzglesI.h:203-213`, `jwxyz/jwzglesI.h:216-220`, `jwxyz/jwzglesI.h:225-235`, `jwxyz/jwzglesI.h:276-286`, `jwxyz/jwzglesI.h:349`, `jwxyz/jwzglesI.h:352-359`, `jwxyz/jwzglesI.h:383-393`).

The six adjacent GLU helpers, not included in 166, are `gluBuild2DMipmaps`, `gluCheckExtension`, `gluErrorString`, `gluLookAt`, `gluPerspective`, and `gluProject` (`jwxyz/jwzglesI.h:362-382`, `jwxyz/jwzglesI.h:394`).

## Hard cases

**`glBegin` / `glEnd` batching.** `jwzgles_glBegin` resets a dynamically grown vertex set and records its primitive (`jwxyz/jwzgles.c:960-997`). Vertex, normal, texture-coordinate, and color wrappers update parallel arrays/current values; at `jwzgles_glEnd`, constant attributes stay scalar while varying attributes enable client arrays, then one wrapped `glDrawArrays` is issued and prior array enablement is restored (`jwxyz/jwzgles.c:1951-2005`, `jwxyz/jwzgles.c:2015-2117`).

**Display lists.** `jwzgles_glNewList` selects a list and records the prevailing enable state (`jwxyz/jwzgles.c:788-829`); `list_push` serializes wrapped calls and snapshots arrays for `glDrawArrays` (`jwxyz/jwzgles.c:832-956`). `optimize_arrays` coalesces saved float arrays into one persistent VBO at `glEndList` (`jwxyz/jwzgles.c:2127-2219`), and `jwzgles_glCallList` replays the recorded operations, including nested lists (`jwxyz/jwzgles.c:2233-2258`).

**`glColorMaterial`.** `jwzgles_glColorMaterial` is an intentional no-op: its implementation is compiled out and the comment says GLES color arrays do not distinguish color from material (`jwxyz/jwzgles.c:1597-1618`). The effective workaround is in `jwzgles_glEnd`: material calls were translated into per-vertex colors, so it temporarily enables `GL_COLOR_MATERIAL` around the draw (`jwxyz/jwzgles.c:2092-2116`).

**Matrix stack.** The shim calls the real GLES1 matrix operations and, when `TRACK_MATRIXES` is enabled, maintains independent modelview, projection, and texture stacks (`jwxyz/jwzgles.c:3713-3725`, `jwxyz/jwzgles.c:3823-3907`). This shadow is required because a GLES1.0 implementation may not provide `glGetFloatv`; the file explicitly describes the doubled tracking work (`jwxyz/jwzgles.c:3806-3820`). Queries return the shadow matrices (`jwxyz/jwzgles.c:4163-4174`).

**`GL_QUADS`.** `convert_quads_to_triangles` expands each four-vertex group, including all parallel attribute arrays, to six vertices and changes the mode to `GL_TRIANGLES`; `GL_QUAD_STRIP` is relabeled `GL_TRIANGLE_STRIP`, and `GL_POLYGON` becomes `GL_TRIANGLE_FAN` (`jwxyz/jwzgles.c:1902-1948`, `jwxyz/jwzgles.c:1972-1979`).

**Texture formats rejected by GLES.** `jwzgles_glTexImage2D` maps numeric component counts 1 through 4 to luminance, luminance-alpha, RGB, or RGBA, allocates zeroed storage when desktop callers pass a null data pointer, promotes RGB storage for RGBA input, and maps `GL_UNSIGNED_INT_8_8_8_8_REV` to `GL_UNSIGNED_BYTE` (`jwxyz/jwzgles.c:3117-3172`). The mipmap helper also maps numeric formats and nearest-neighbor expands non-power-of-two RGB/RGBA data to power-of-two RGBA, but uploads only level zero (`jwxyz/jwzgles.c:3459-3532`). Texture parameters downgrade mipmap filters, map 1D to 2D, and ignore `GL_CLAMP` wrapping (`jwxyz/jwzgles.c:4420-4457`).

## Gap table

Status describes wayshade's current implementation, not an aspiration.

| Capability | jwz's approach | Ours |
|---|---|---|
| Immediate `glBegin` batching | Parallel client arrays, constant-attribute optimization, one wrapped draw at end (`jwxyz/jwzgles.c:1951-2117`). | **WE DID IT DIFFERENTLY.** One interleaved 12-float vertex record (position, normal, RGBA, UV), scratch VBO upload, shader uniforms, then GLES3 draw (`src/gles3_compat.c:1177-1227`, `src/gles3_compat.c:1312-1464`). |
| Primitive coverage | Quads expand to six vertices; quad strip and polygon are relabeled (`jwxyz/jwzgles.c:1902-1979`). | **WE HAVE IT.** Quads and quad strips use generated index buffers; polygon maps to triangle fan; points, lines, line strips/loops, triangles, fans, and strips pass through (`src/gles3_compat.c:1330-1418`). |
| Display lists | Records a broad set of wrapped calls; snapshots draw arrays; compacts float data into a persistent VBO; supports nested calls (`jwxyz/jwzgles.c:832-956`, `jwxyz/jwzgles.c:2127-2258`). | **WE LACK IT.** We have list allocation/replay and inline batch or chain capture (`src/gles3_compat.c:1084-1153`, `src/gles3_compat.c:1472-1486`), but our recorder is an explicit fixed operation set rather than jwz's general wrapped-call record (`src/gles3_compat.c:1062-1082`). |
| Matrix stack | Uses GLES1 fixed-function matrices and shadows all three stacks for queries (`jwxyz/jwzgles.c:3713-3725`, `jwxyz/jwzgles.c:3806-3820`). | **WE DID IT DIFFERENTLY.** CPU modelview/projection stacks feed a GLES3 shader; the current state has only those two stacks (`src/gles3_compat.c:126-137`, `src/gles3_compat.c:800-851`, `src/gles3_compat.c:1421-1434`). We lack a texture matrix stack. |
| `glColorMaterial` | Selector is a no-op; material calls become colors and color-material is enabled for the draw (`jwxyz/jwzgles.c:1597-1618`, `jwxyz/jwzgles.c:2092-2116`). | **WE DID IT DIFFERENTLY.** We store enable, face, and mode and route live color into one material slot (`src/gles3_compat.c:174-188`, `src/gles3_compat.c:1261-1277`, `src/gles3_compat.c:2453-2487`). Ambient/diffuse are not independently represented (`src/gles3_compat.c:1264-1273`). |
| Lighting/material breadth | Delegates GLES1 lighting and wraps scalar/vector integer/float variants (`jwxyz/jwzglesI.h:272-275`, `jwxyz/jwzglesI.h:287-294`). | **WE LACK IT.** Shader state supports one directional light and ambient/diffuse/specular inputs (`src/gles3_compat.c:944-995`); light-model calls are no-ops and material handling is a limited translation (`src/gles3_compat.c:2192-2234`). |
| Legacy vertex overloads | Implements 2D/3D/4D vertex forms, eight color scalar types plus vectors, and 1D through 4D texture coordinates (`jwxyz/jwzglesI.h:168-188`, `jwxyz/jwzglesI.h:240-271`). | **WE LACK IT.** The direct GL wrappers cover a narrower set centered on float/double 2D/3D vertices, float colors plus `glColor4ub`, float/double 2D UVs, and float/double normals (`src/gles3_compat.c:2325-2395`). |
| Client arrays/interleaved arrays | Tracks pointers, buffer bindings, enablement and many GL1 interleaved formats (`jwxyz/jwzgles.c:2808-3007`, `jwxyz/jwzgles.c:4327-4417`). | **WE HAVE IT.** We track four client arrays and translate draw arrays/interleaved layouts into the GLES3 path (`src/gles3_compat.c:2489-2527`, `src/gles3_compat.c:2529-2647`). |
| Texture compatibility | 1D-to-2D mapping, 2D/3D upload, copy/subimage, texgen, rejected-format normalization, and a level-zero pseudo-mipmap path (`jwxyz/jwzgles.c:3095-3255`, `jwxyz/jwzgles.c:3459-3532`). | **WE LACK IT.** Our GL1 `glTexImage1D` wrapper is a no-op (`src/gles3_compat.c:2296-2300`). Our GLU path does better for one case by converting luminance-alpha and invoking real `glGenerateMipmap`, but only accepts `GL_TEXTURE_2D` (`src/xscreensaver_compat.c:326-419`). |
| Fog, alpha test, logic op, polygon mode | Wrappers exist, although the source explicitly lists polygon line/point as unsupported (`jwxyz/jwzgles.c:120-141`, `jwxyz/jwzglesI.h:352-357`). | **WE LACK IT.** Fog and alpha-test wrappers are no-ops, as are polygon mode and logic op (`src/gles3_compat.c:2191-2194`, `src/gles3_compat.c:2250-2252`, `src/gles3_compat.c:2276`, `src/gles3_compat.c:2289`). |
| Queries | Shadows current color, array state and matrices, delegating other queries (`jwxyz/jwzgles.c:4126-4203`). | **WE LACK IT.** Our query wrappers cover a small explicit subset (`src/gles3_compat.c:2302-2323`, `src/gles3_compat.c:2652-2673`). |
| GLU | Six helpers adjacent to the GL surface (`jwxyz/jwzglesI.h:362-394`). | **WE HAVE IT, PARTLY DIFFERENT.** Perspective, look-at and project are CPU implementations (`src/xscreensaver_compat.c:79-155`); image scaling is RGBA8-only (`src/xscreensaver_compat.c:299-323`); mipmaps use GLES3 generation (`src/xscreensaver_compat.c:326-419`). |
| X11-facing platform shim | `jwxyz` is explicitly the non-Xlib substrate (`jwxyz/jwzgles.c:12-19`), with Cocoa context routing in `jwxyz-cocoa.m` (`jwxyz/jwxyz-cocoa.m:866-1004`). | **WE DID IT DIFFERENTLY.** `xscreensaver_compat.c` routes `glX*` through harness-owned EGL globals (`src/xscreensaver_compat.c:12-32`, `src/xscreensaver_compat.c:422-452`). |

The three largest engineering gaps are therefore (1) the breadth and fidelity of legacy GL overloads and state/query behavior, (2) general display-list recording including arbitrary state calls and durable GPU array storage, and (3) texture compatibility, especially 1D, format normalization, copy/subimage, texgen, and legacy parameter behavior. These are larger than the missing Apple window-system backend itself if the goal is catalogue-wide fidelity rather than running the already-ported subset.

## Portable core versus Apple-specific backend

### Portable prior art

The algorithms in `jwzgles.c` are not Cocoa APIs: immediate-mode accumulation, display-list serialization/VBO packing, quad conversion, legacy texture normalization, and matrix shadowing operate on C data plus GL/GLES calls (`jwxyz/jwzgles.c:670-681`, `jwxyz/jwzgles.c:832-956`, `jwxyz/jwzgles.c:1902-1948`, `jwxyz/jwzgles.c:2127-2219`, `jwxyz/jwzgles.c:3117-3172`). Those techniques can be implemented above GLES3, Metal, Direct3D, or another draw backend. `jwxyz` also separates common operations from its rendering substrate; `jwzgles.c` directs readers to the division-of-labor explanation (`jwxyz/jwzgles.c:12-15`).

### macOS-specific work

The macOS host creates an `NSOpenGLContext`, sets swap interval, binds it to the view, and requests a best-resolution surface (`OSX/XScreenSaverView.m:1021-1059`). It uses the `ScreenSaverView` lifecycle: `startAnimation` defers saver initialization until the first `animateOneFrame` (`OSX/XScreenSaverView.m:968-982`), `animateOneFrame` invokes the X11 render adapter (`OSX/XScreenSaverView.m:2236-2265`), and `stopAnimation` calls the hack free callback, destroys the emulated display/window, and clears the current context (`OSX/XScreenSaverView.m:1151-1228`). Retina sizing converts view points with `window.backingScaleFactor` and sets a pixel viewport (`OSX/XScreenSaverView.m:1280-1310`, `OSX/XScreenSaverView.m:1543-1568`). A wayshade macOS backend must supply equivalent lifecycle ownership, native view/context or Metal-layer creation, frame scheduling/presentation, resize, input, preferences/bundle glue, and point-to-pixel scaling; `jwzgles` does none of those jobs.

### iOS-specific work

The iOS host exposes a `CAEAGLLayer`, sets its drawable properties and native screen scale, creates an `EAGLContext` for GLES1 or GLES3, and makes it current (`OSX/XScreenSaverView.m:1090-1127`). Resizing obtains renderbuffer storage from that layer and attaches it to the framebuffer (`OSX/XScreenSaverView.m:928-964`). iOS uses `contentScaleFactor` rather than the macOS window backing scale (`OSX/XScreenSaverView.m:1292-1298`, `OSX/XScreenSaverView.m:1551-1555`) and maps app backgrounding into saver locking (`OSX/SaverRunner.m:1704-1723`). These are UIKit/Core Animation lifecycle details, not reusable GL1 emulation.

### Decision consequence

wayshade can reuse its portable GLES3 emulation design on macOS, but none of the inspected code gives it a modern macOS host for free. The upstream macOS path is `NSOpenGLContext`, while its mobile path is the Apple-specific EAGL/CAEAGLLayer stack (`OSX/XScreenSaverView.m:1021-1127`). A native backend still has to be written around wayshade's renderer. The portable catalogue problem is bounded by the 166-entry wrapper surface, but the effective requirement is smaller only if wayshade intentionally supports its current curated saver set; upstream explicitly documents catalogue features that remain visually wrong even with jwzgles (`jwxyz/jwzgles.c:120-159`).

## License position

`jwzgles.c` carries Jamie Zawinski's permissive notice. It grants, without fee, permission to use, copy, modify, distribute, and sell the software and documentation for any purpose, provided the copyright notice appears in all copies and both the copyright and permission notices appear in supporting documentation; it disclaims suitability and all express or implied warranty (`jwxyz/jwzgles.c:1-9`). This is not a copyleft license and does not impose source-disclosure or same-license requirements.

At this repository's root, `LICENSE` is Apache License 2.0 (`LICENSE:2-6`), which grants reproduction, derivative-work, sublicensing, and distribution rights (`LICENSE:67-72`) subject to its redistribution conditions (`LICENSE:90-129`). However, `meson.build` declares the project `GPL-2.0-or-later` (`meson.build:27`). Therefore the single-label license identity of this repository is **UNKNOWN** from the checked files; resolving it requires the maintainers to say whether the Meson declaration or root license governs which components.

The practical compatibility finding is narrower and factual: jwz's notice permits his code to be redistributed in an Apache-2.0 or GPL-2.0-or-later aggregate if its notice and supporting-documentation condition are preserved. It does not supply an express patent grant. This document copies no implementation and uses only short behavioral descriptions, so it adds no jwz source-code licensing obligation to wayshade.

No technique identified here is legally restricted to clean-room reimplementation by jwz's notice: copying is expressly permitted under its conditions (`jwxyz/jwzgles.c:3-9`). For this task and repository policy, however, **all** source-shaped details must be independently implemented from documented behavior rather than transcribed: immediate batching, display-list recording/VBO packing, color-material translation, matrix shadowing, quad expansion, and texture normalization. This reference records observable structure and API behavior only.

## Port-size assessment

The catalogue-wide port is materially larger than a naive reading of "we already have `gles3_compat.c`" suggests. wayshade already owns the core rendering spine - immediate batching, common primitive conversion, CPU matrices, shader lighting, client arrays, and basic lists (`src/gles3_compat.c:1159-1495`, `src/gles3_compat.c:2044-2128`, `src/gles3_compat.c:2489-2647`). The remaining work is concentrated but broad: overload/state/query completeness, general display-list semantics, and legacy texture behavior. jwz's own mature shim still names unsupported features and visibly wrong savers (`jwxyz/jwzgles.c:120-159`), so 166 is an empirical upper boundary for accepted calls, not a promise of exact desktop GL1 rendering. Separately, Windows and macOS each require native lifecycle, surface, input, and presentation backends; the Apple wrapper demonstrates those responsibilities but is not portable code (`OSX/XScreenSaverView.m:968-1128`, `OSX/XScreenSaverView.m:1151-1228`).
