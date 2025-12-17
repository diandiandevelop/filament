/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TNT_FILAMENT_SAMPLE_IBL_H
#define TNT_FILAMENT_SAMPLE_IBL_H

#include <filament/Texture.h>

#include <math/vec3.h>

#include <string>
#include <sstream>

namespace filament {
class Engine;
class IndexBuffer;
class IndirectLight;
class Material;
class MaterialInstance;
class Renderable;
class Texture;
class Skybox;
}

namespace utils {
    class Path;
}

/**
 * IBL - 基于图像的光照（Image-Based Lighting）类
 * 
 * 负责加载和管理IBL资源，包括：
 * - 环境贴图（Skybox）
 * - 间接光照（IndirectLight）
 * - 雾效纹理（FogTexture）
 * - 球谐函数（Spherical Harmonics）
 * 
 * 支持从等距圆柱投影图（Equirectangular）、目录或KTX格式加载。
 */
class IBL {
public:
    /**
     * 构造函数
     * @param engine Filament引擎引用
     */
    explicit IBL(filament::Engine& engine);
    /** 析构函数：清理资源 */
    ~IBL();

    /**
     * 从等距圆柱投影图加载IBL
     * @param path 图像文件路径（支持HDR/EXR格式）
     * @return 加载是否成功
     */
    bool loadFromEquirect(const utils::Path& path);
    /**
     * 从目录加载IBL
     * 目录应包含立方体贴图的六个面（px, nx, py, ny, pz, nz）
     * 以及可选的sh.txt球谐函数文件
     * @param path 目录路径
     * @return 加载是否成功
     */
    bool loadFromDirectory(const utils::Path& path);
    /**
     * 从KTX格式文件加载IBL
     * @param prefix 文件路径前缀，会自动添加_ibl.ktx和_skybox.ktx后缀
     * @return 加载是否成功
     */
    bool loadFromKtx(const std::string& prefix);

    /**
     * 获取间接光照对象
     * @return 间接光照对象指针
     */
    filament::IndirectLight* getIndirectLight() const noexcept {
        return mIndirectLight;
    }

    /**
     * 获取天空盒对象
     * @return 天空盒对象指针
     */
    filament::Skybox* getSkybox() const noexcept {
        return mSkybox;
    }

    /**
     * 获取雾效纹理
     * @return 雾效纹理指针
     */
    filament::Texture* getFogTexture() const noexcept {
        return mFogTexture;
    }

    /**
     * 检查是否包含球谐函数数据
     * @return true包含，false不包含
     */
    bool hasSphericalHarmonics() const { return mHasSphericalHarmonics; }
    /**
     * 获取球谐函数系数
     * @return 球谐函数系数数组指针（9个float3，对应3阶球谐）
     */
    filament::math::float3 const* getSphericalHarmonics() const { return mBands; }

private:
    /**
     * 加载立方体贴图的某一级Mipmap
     * @param texture 输出的纹理指针
     * @param path 资源路径
     * @param level Mipmap级别，默认0（最高级别）
     * @param levelPrefix 级别前缀（如"m0_"）
     * @return 加载是否成功
     */
    bool loadCubemapLevel(filament::Texture** texture, const utils::Path& path,
            size_t level = 0, std::string const& levelPrefix = "") const;


    /**
     * 加载立方体贴图的某一级Mipmap（带缓冲区输出）
     * @param texture 输出的纹理指针
     * @param outBuffer 输出的像素缓冲区描述符
     * @param dim 输出的纹理尺寸
     * @param path 资源路径
     * @param level Mipmap级别，默认0
     * @param levelPrefix 级别前缀
     * @return 加载是否成功
     */
    bool loadCubemapLevel(filament::Texture** texture,
            filament::Texture::PixelBufferDescriptor* outBuffer,
            uint32_t* dim,
            const utils::Path& path,
            size_t level = 0, std::string const& levelPrefix = "") const;

    filament::Engine& mEngine;                    // Filament引擎引用

    filament::math::float3 mBands[9] = {};       // 球谐函数系数（3阶，9个系数）
    bool mHasSphericalHarmonics = false;         // 是否包含球谐函数数据

    filament::Texture* mTexture = nullptr;       // IBL反射贴图（带Mipmap）
    filament::IndirectLight* mIndirectLight = nullptr; // 间接光照对象
    filament::Texture* mSkyboxTexture = nullptr; // 天空盒纹理
    filament::Texture* mFogTexture = nullptr;    // 雾效纹理
    filament::Skybox* mSkybox = nullptr;         // 天空盒对象
};

#endif // TNT_FILAMENT_SAMPLE_IBL_H
