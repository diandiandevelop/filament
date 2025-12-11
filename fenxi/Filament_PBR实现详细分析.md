# Filament PBR 实现详细分析

## 目录
- 概览
- 工作流与参数
- 直射光 BRDF（Cook–Torrance）
- IBL 与 LUT
- 扩展特性（Clear Coat / Sheen / Anisotropy）
- 关键代码与 Shader 路径
- 能量守恒与补偿

---

## 概览
- 工作流：金属度-粗糙度（Metallic-Roughness）。
- 模型：Cook–Torrance 微表面，GGX NDF + Smith GGX 可见性 + Schlick 菲涅耳。
- 分离直射与间接：直射光逐像素计算；间接光用预滤环境图 + BRDF LUT。
- 能量守恒：漫反射与镜面分摊 + 多次散射补偿（高粗糙度不变暗）。

---

## 工作流与参数
- `baseColor`：金属时为 F0 基底，介电时为漫反射基色。
- `metallic`：0..1 插值漫反射/镜面能量。
- `roughness`：GGX 粗糙度，驱动 NDF 与可见性。
- 贴图：`normal`、`occlusion`（影响间接光）、`emissive`。
- 扩展：`clearCoat`、`sheenColor`、`anisotropy`、`transmission` / `subsurface`（按材质配置编译宏）。

---

## 直射光 BRDF（Cook–Torrance）
- NDF：GGX（`distribution()` → `D_GGX`）。
- 可见性：Smith GGX（`visibility()` → `V_SmithGGXCorrelated`），移动端可切 fast 近似。
- 菲涅耳：Schlick，f90 由 F0 推导；金属直接用 baseColor，介电 F0≈0.04。
- 漫反射：`diffuse()`（Lambert/Burley），金属时漫反射为 0。
- 组合：`specularLobe()` + `diffuseLobe()` → `surfaceShading()`。
- 能量补偿：`pixel.energyCompensation` 抵消高粗糙度下的能量损失。

---

## IBL 与 LUT
- 预滤环境：离线对 HDR 环境图做 GGX importance sampling 生成 mip 链。
- BRDF LUT：离线 2D LUT（DFG）存放 `f_a`/`f_b`，运行时用 N·V、roughness 查表。
- 运行时：采样预滤环境的合适 mip + LUT，再结合漫反射积分的低频环境。

---

## 扩展特性
- Clear Coat：独立 GGX 层，固定 IOR≈1.5，使用几何法线避免法线贴图细节叠加。
- Sheen：织物高光（Charlie NDF + Neubelt 可见性）。
- Anisotropy：切线/副切线方向独立粗糙度，基于 GGX 各向异性。
- Transmission/Subsurface：简化近似（按材质宏决定）。

---

## 关键代码与 Shader 路径
- BRDF 入口与分发：`shaders/src/surface_shading_model_standard.fs`
  - `specularLobe()` / `diffuseLobe()` / `surfaceShading()`
- BRDF 具体实现：`shaders/src/surface_brdf.fs`
  - `D_GGX` / `V_SmithGGXCorrelated` / `F_Schlick` / `distribution*` / `visibility*`
- 材质输入定义：`shaders/src/surface_material_inputs.fs`
- IBL 反射：`shaders/src/surface_light_reflections.fs`
- 材质与宏控制：`materials` 打包的模板（生成资源）

---

## 能量守恒与补偿
- 漫反射与镜面按 `metallic` 重新分配能量；镜面能量越高，漫反射越低。
- 高粗糙度时使用 `energyCompensation` 防止多次散射导致整体变暗。
- Clear Coat 通过 `attenuation = 1 - Fcc` 将底层能量衰减，避免双倍能量。

---

## 摘要
Filament 的 PBR 基于 Cook–Torrance + GGX + Schlick，并通过预滤 IBL、BRDF LUT 以及多次散射补偿确保在移动端与桌面端都能获得稳定的一致性和能量守恒表现。扩展特性（Clear Coat / Sheen / Anisotropy）按需通过宏编译进入 Shader，保证性能与灵活性。 

