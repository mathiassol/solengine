---
title: Material Editor
description: Editing PBR materials with the SolEngine Material Editor.
---

# Material Editor

The Material Editor lets you tweak PBR material properties on any `MeshNode` or
`ModelNode` sub-mesh and see a live preview without leaving the editor.

---

## Opening the Material Editor

1. Select a `MeshNode` or `ModelNode` in the **Hierarchy**.
2. Go to **Window → Material Editor** (or the **Material Editor** tab in the main dock).
3. The editor automatically tracks the selected node.

---

## Interface

The editor is split into two panels:

### Left Panel — Properties

**Sub-mesh picker** (top bar) — for `ModelNode` with multiple sub-meshes, select which
material to edit.

#### Base Material
| Property | Description |
|----------|-------------|
| **Base Color** | RGBA albedo. Alpha drives transparency in Blend/Mask mode. |
| **Metallic** | 0 = dielectric, 1 = metal. |
| **Roughness** | 0 = mirror-smooth, 1 = fully diffuse. |

#### Emissive
| Property | Description |
|----------|-------------|
| **Emissive Color** | RGB glow colour. Multiplied with emissive texture if present. |
| **Emissive Strength** | Scalar multiplier (0–20). |

#### Transparency
| Property | Description |
|----------|-------------|
| **Alpha Mode** | `Opaque`, `Mask` (alpha cut-out), or `Blend` (alpha blending). |
| **Alpha Cutoff** | Threshold for Mask mode (0–1). |

#### Flags
| Property | Description |
|----------|-------------|
| **Lit** | Toggle PBR lighting on/off for this material. |
| **Double Sided** | Disables back-face culling. |

#### Textures
Texture paths are relative to the project root. Click the **…** button to open a
file browser.

| Slot | Description |
|------|-------------|
| **Albedo** | sRGB base colour texture. Alpha channel used for transparency. |
| **Normal** | Tangent-space normal map (OpenGL convention, Y-up). |
| **Metallic / Roughness** | R = metallic, G = roughness (glTF packed format). |
| **Emissive** | sRGB emissive colour texture. |

After setting paths, click **Reload Textures** to apply them immediately.

### Right Panel — Preview Sphere

A **48 × 48** CPU-rasterised sphere with:
- GGX specular + Lambert diffuse (Cook-Torrance BRDF)
- Fixed directional light from upper-left
- ACES tone-mapping

The preview refreshes automatically whenever you change a material property.
It does **not** require a running game session.

---

## Saving Materials

Material changes on `MeshNode` are stored directly in the `.solscene` file when
you hit **File → Save Scene** (or `Ctrl+S`). No separate material file format is used.

---

## Lua Access

You can also read and modify material properties from Lua:

```lua
local mesh = Scene.get_node("MyBox")

-- Read
local color = mesh:get_material_base_color()   -- Vec4

-- Write
mesh:set_material_base_color(Vec4(1, 0, 0, 1))   -- red
mesh:set_material_metallic(0.8)
mesh:set_material_roughness(0.2)
mesh:set_material_emissive(Vec3(1, 0.5, 0))
```
