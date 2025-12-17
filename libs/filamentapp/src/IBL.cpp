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

#include <filamentapp/IBL.h>

#include <filament/Engine.h>
#include <filament/IndirectLight.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>

#include <ktxreader/Ktx1Reader.h>

#include <imageio/ImageDecoder.h>

#include <filament-iblprefilter/IBLPrefilterContext.h>

#include <stb_image.h>

#include <utils/Path.h>

#include <fstream>
#include <iostream>
#include <string>

#include <string.h>

#include <utils/Log.h>

using namespace filament;
using namespace filament::math;
using namespace ktxreader;
using namespace utils;

/**
 * IBL强度常量
 * 
 * 用于设置间接光照的强度值，单位为勒克斯（lux）。
 * 这个值用于将HDR环境贴图的亮度映射到场景中。
 */
static constexpr float IBL_INTENSITY = 30000.0f;

/**
 * 构造函数实现
 * 
 * 执行步骤：
 * 1. 保存引擎引用
 * 
 * @param engine Filament引擎引用
 */
IBL::IBL(Engine& engine) : mEngine(engine) {
}

/**
 * 析构函数实现
 * 
 * 执行步骤：
 * 1. 销毁间接光照对象
 * 2. 销毁反射贴图纹理
 * 3. 销毁天空盒对象
 * 4. 销毁天空盒纹理
 * 5. 销毁雾效纹理
 */
IBL::~IBL() {
    mEngine.destroy(mIndirectLight);
    mEngine.destroy(mTexture);
    mEngine.destroy(mSkybox);
    mEngine.destroy(mSkyboxTexture);
    mEngine.destroy(mFogTexture);
}

/**
 * 从等距圆柱投影（Equirectangular）图像加载IBL实现
 * 
 * 执行步骤：
 * 1. 检查文件是否存在
 * 2. 根据文件格式加载图像数据（EXR或普通图像）
 * 3. 验证图像格式和尺寸（必须是2:1宽高比的RGB图像）
 * 4. 创建等距圆柱投影纹理
 * 5. 使用IBL预过滤器将等距圆柱投影转换为立方体贴图
 * 6. 生成反射贴图（预过滤的环境贴图，带Mipmap）
 * 7. 生成辐照度贴图（用于漫反射和雾效）
 * 8. 创建间接光照对象和天空盒
 * 
 * @param path 等距圆柱投影图像文件路径
 * @return 加载是否成功
 */
bool IBL::loadFromEquirect(Path const& path) {
    // 检查文件是否存在
    if (!path.exists()) {
        return false;
    }

    // 图像数据变量
    int w = 0, h = 0;  // 图像宽度和高度
    int n = 0;          // 图像通道数
    size_t size = 0;    // 图像数据大小（字节）
    void* data = nullptr;  // 图像数据指针
    void* user = nullptr;  // 用户数据指针（用于释放回调）
    Texture::PixelBufferDescriptor::Callback destroyer{};  // 数据释放回调函数

    // 根据文件扩展名选择加载方式
    if (path.getExtension() == "exr") {
        // EXR格式：使用ImageDecoder加载线性图像
        std::ifstream in_stream(path.getAbsolutePath().c_str(), std::ios::binary);
        image::LinearImage* image = new image::LinearImage(
                image::ImageDecoder::decode(in_stream, path.getAbsolutePath().c_str()));
        w = image->getWidth();
        h = image->getHeight();
        n = image->getChannels();
        size = w * h * n * sizeof(float);
        data = image->getPixelRef();
        user = image;
        // 设置释放回调：删除LinearImage对象
        destroyer = [](void*, size_t, void* user) {
            delete reinterpret_cast<image::LinearImage*>(user);
        };
    } else {
        // 其他格式：使用stb_image加载为浮点数格式
        stbi_info(path.getAbsolutePath().c_str(), &w, &h, nullptr);
        // load image as float
        // 加载图像为浮点数格式（强制3通道RGB）
        size = w * h * sizeof(float3);
        data = (float3*)stbi_loadf(path.getAbsolutePath().c_str(), &w, &h, &n, 3);
        // 设置释放回调：使用stbi_image_free释放内存
        destroyer = [](void* data, size_t, void*) {
            stbi_image_free(data);
        };
    }

    // 验证图像数据是否加载成功且为RGB格式（3通道）
    if (data == nullptr || n != 3) {
        std::cerr << "Could not decode image " << std::endl;
        destroyer(data, size, user);
        return false;
    }

    // 验证是否为等距圆柱投影图像（宽高比必须为2:1）
    if (w != h * 2) {
        std::cerr << "not an equirectangular image!" << std::endl;
        destroyer(data, size, user);
        return false;
    }

    // 创建等距圆柱投影纹理的像素缓冲区描述符
    // 使用RGB格式、FLOAT类型，并设置释放回调
    Texture::PixelBufferDescriptor buffer(
            data, size,Texture::Format::RGB, Texture::Type::FLOAT, destroyer, user);

    // 创建等距圆柱投影纹理对象
    // 使用R11F_G11F_B10F格式（RGB10_11_11_REV，节省内存）
    // 设置自动生成所有Mipmap级别（0xff表示所有级别）
    Texture* const equirect = Texture::Builder()
            .width((uint32_t)w)
            .height((uint32_t)h)
            .levels(0xff)  // 自动生成所有Mipmap级别
            .format(Texture::InternalFormat::R11F_G11F_B10F)  // 使用RGB10_11_11_REV格式
            .sampler(Texture::Sampler::SAMPLER_2D)  // 2D纹理采样器
            .usage(Texture::Usage::DEFAULT | Texture::Usage::GEN_MIPMAPPABLE)  // 允许生成Mipmap
            .build(mEngine);

    // 设置纹理图像数据（第0级Mipmap）
    equirect->setImage(mEngine, 0, std::move(buffer));

    // 创建IBL预过滤上下文和过滤器
    IBLPrefilterContext context(mEngine);
    IBLPrefilterContext::EquirectangularToCubemap equirectangularToCubemap(context);
    IBLPrefilterContext::SpecularFilter specularFilter(context);
    IBLPrefilterContext::IrradianceFilter irradianceFilter(context);

    // 步骤1：将等距圆柱投影图转换为立方体贴图（用于天空盒）
    mSkyboxTexture = equirectangularToCubemap(equirect);

    mEngine.destroy(equirect);

    // 步骤2：生成反射贴图（预过滤的环境贴图，带Mipmap，用于镜面反射）
    mTexture = specularFilter(mSkyboxTexture);

    // 步骤3：生成辐照度贴图（用于漫反射和雾效）
    mFogTexture = irradianceFilter({ .generateMipmap = true }, mSkyboxTexture);
    mFogTexture->generateMipmaps(mEngine);

    // 创建间接光照对象：使用反射贴图和强度
    mIndirectLight = IndirectLight::Builder()
            .reflections(mTexture)
            .intensity(IBL_INTENSITY)
            .build(mEngine);

    // 创建天空盒：使用立方体贴图，显示太阳
    mSkybox = Skybox::Builder()
            .environment(mSkyboxTexture)
            .showSun(true)
            .build(mEngine);

    return true;
}

/**
 * 从KTX格式文件加载IBL实现
 * 
 * 执行步骤：
 * 1. 检查IBL和天空盒KTX文件是否存在
 * 2. 读取KTX文件并创建Ktx1Bundle对象
 * 3. 使用Ktx1Reader创建纹理对象
 * 4. 从KTX文件提取球谐函数系数
 * 5. 创建间接光照对象和天空盒
 * 
 * 注意：雾效纹理暂未实现，因为IBLPrefilter要求源图像有Mipmap级别，
 * 而KTX文件可能没有或无法生成（例如压缩纹理）。
 * 
 * @param prefix 文件路径前缀（会自动添加_ibl.ktx和_skybox.ktx后缀）
 * @return 加载是否成功
 */
bool IBL::loadFromKtx(const std::string& prefix) {
    Path iblPath(prefix + "_ibl.ktx");
    if (!iblPath.exists()) {
        return false;
    }
    Path skyPath(prefix + "_skybox.ktx");
    if (!skyPath.exists()) {
        return false;
    }

    // Lambda函数：从文件路径创建Ktx1Bundle对象
    // 执行步骤：
    // 1. 以二进制模式打开文件
    // 2. 读取整个文件内容到vector
    // 3. 使用文件数据创建Ktx1Bundle对象
    auto createKtx = [] (Path path) {
        using namespace std;
        ifstream file(path.getPath(), ios::binary);  // 以二进制模式打开文件
        vector<uint8_t> contents((istreambuf_iterator<char>(file)), {});  // 读取整个文件内容
        return new image::Ktx1Bundle(contents.data(), contents.size());  // 创建KTX包对象
    };

    // 创建KTX包对象
    Ktx1Bundle* iblKtx = createKtx(iblPath);
    Ktx1Bundle* skyKtx = createKtx(skyPath);

    // 使用Ktx1Reader创建纹理对象
    mSkyboxTexture = Ktx1Reader::createTexture(&mEngine, skyKtx, false);
    mTexture = Ktx1Reader::createTexture(&mEngine, iblKtx, false);

    // TODO: create the fog texture, it's a bit complicated because IBLPrefilter requires
    //       the source image to have miplevels, and it's not guaranteed here and also
    //       not guaranteed we can generate them (e.g. texture could be compressed)
    // 创建雾效纹理，这有点复杂，因为IBLPrefilter要求源图像有Mipmap级别，
    // 而这里不能保证有，也不能保证可以生成（例如纹理可能是压缩的）
    //IBLPrefilterContext context(mEngine);
    //IBLPrefilterContext::IrradianceFilter irradianceFilter(context);
    //mFogTexture = irradianceFilter({ .generateMipmap=false }, mSkyboxTexture);
    //mFogTexture->generateMipmaps(mEngine);


    // 从KTX文件提取球谐函数系数
    if (!iblKtx->getSphericalHarmonics(mBands)) {
        return false;
    }
    mHasSphericalHarmonics = true;

    // 创建间接光照对象
    mIndirectLight = IndirectLight::Builder()
            .reflections(mTexture)
            .intensity(IBL_INTENSITY)
            .build(mEngine);

    // 创建天空盒
    mSkybox = Skybox::Builder().environment(mSkyboxTexture).showSun(true).build(mEngine);

    return true;
}

/**
 * 从目录加载IBL实现
 * 
 * 执行步骤：
 * 1. 首先尝试加载KTX格式文件（如果存在）
 * 2. 读取球谐函数文件（sh.txt）
 * 3. 解析球谐函数系数（9个float3，格式为"(r,g,b)"）
 * 4. 加载Mipmap级别的立方体贴图（m0_, m1_, m2_...）
 * 5. 加载天空盒立方体贴图（无前缀）
 * 6. 创建间接光照对象（使用反射贴图和球谐函数）
 * 7. 创建天空盒
 * 
 * @param path 目录路径
 * @return 加载是否成功
 */
bool IBL::loadFromDirectory(const utils::Path& path) {
    // First check if KTX files are available.
    // 首先检查是否有KTX格式文件
    if (loadFromKtx(Path::concat(path, path.getName()))) {
        return true;
    }
    // Read spherical harmonics
    // 读取球谐函数文件
    Path sh(Path::concat(path, "sh.txt"));
    if (sh.exists()) {
        std::ifstream shReader(sh);
        shReader >> std::skipws;

        // 解析球谐函数系数（9个系数，每个格式为"(r,g,b)"）
        std::string line;
        for (float3& band : mBands) {
            std::getline(shReader, line);
            int n = sscanf(line.c_str(), "(%f,%f,%f)", &band.r, &band.g, &band.b); // NOLINT(cert-err34-c)
            if (n != 3) return false;
        }
    } else {
        return false;
    }
    mHasSphericalHarmonics = true;

    // Read mip-mapped cubemap
    // 读取带Mipmap的立方体贴图
    const std::string prefix = "m";
    // 加载第0级Mipmap（最高级别）
    if (!loadCubemapLevel(&mTexture, path, 0, prefix + "0_")) return false;

    // 加载其他Mipmap级别
    size_t numLevels = mTexture->getLevels();
    for (size_t i = 1; i<numLevels; i++) {
        const std::string levelPrefix = prefix + std::to_string(i) + "_";
        loadCubemapLevel(&mTexture, path, i, levelPrefix);
    }

    // 加载天空盒立方体贴图（无Mipmap，无前缀）
    if (!loadCubemapLevel(&mSkyboxTexture, path)) return false;

    // 创建间接光照对象：使用反射贴图和球谐函数（3阶）
    mIndirectLight = IndirectLight::Builder()
            .reflections(mTexture)
            .irradiance(3, mBands)  // 3阶球谐函数
            .intensity(IBL_INTENSITY)
            .build(mEngine);

    // 创建天空盒
    mSkybox = Skybox::Builder().environment(mSkyboxTexture).showSun(true).build(mEngine);

    return true;
}

/**
 * 加载立方体贴图的某一级Mipmap实现（简化版本）
 * 
 * 执行步骤：
 * 1. 调用完整版本的loadCubemapLevel加载数据
 * 2. 如果成功，将数据设置到纹理的指定Mipmap级别
 * 3. 返回是否成功
 * 
 * @param texture 输出的纹理指针
 * @param path 资源路径
 * @param level Mipmap级别
 * @param levelPrefix 级别前缀
 * @return 加载是否成功
 */
bool IBL::loadCubemapLevel(filament::Texture** texture, const utils::Path& path, size_t level,
        std::string const& levelPrefix) const {
    uint32_t dim;
    Texture::PixelBufferDescriptor buffer;
    if (loadCubemapLevel(texture, &buffer, &dim, path, level, levelPrefix)) {
        // 设置立方体贴图图像（6个面，每个面dim x dim大小）
        (*texture)->setImage(mEngine, level, 0, 0, 0, dim, dim, 6, std::move(buffer));
        return true;
    }
    return false;
}

/**
 * 加载立方体贴图的某一级Mipmap实现（完整版本，带缓冲区输出）
 * 
 * 执行步骤：
 * 1. 检查第一个面的文件是否存在（确定尺寸和Mipmap级别数）
 * 2. 如果是第0级，创建纹理对象
 * 3. 为6个面分配内存缓冲区
 * 4. 逐个加载6个面的图像数据（px, nx, py, ny, pz, nz）
 * 5. 将数据转换为RGB_10_11_11_REV格式
 * 6. 返回像素缓冲区描述符
 * 
 * @param texture 输出的纹理指针
 * @param outBuffer 输出的像素缓冲区描述符
 * @param dim 输出的纹理尺寸
 * @param path 资源路径
 * @param level Mipmap级别
 * @param levelPrefix 级别前缀（如"m0_"）
 * @return 加载是否成功
 */
bool IBL::loadCubemapLevel(
        filament::Texture** texture,
        Texture::PixelBufferDescriptor* outBuffer,
        uint32_t* dim,
        const utils::Path& path, size_t level, std::string const& levelPrefix) const {
    // 立方体贴图6个面的后缀：正X、负X、正Y、负Y、正Z、负Z
    static const char* faceSuffix[6] = { "px", "nx", "py", "ny", "pz", "nz" };

    size_t size = 0;
    size_t numLevels = 1;

    { // this is just a scope to avoid variable name hiding below
        // 检查第一个面的文件，确定纹理尺寸和Mipmap级别数
        int w, h;
        std::string faceName = levelPrefix + faceSuffix[0] + ".rgb32f";
        Path facePath(Path::concat(path, faceName));
        if (!facePath.exists()) {
            std::cerr << "The face " << faceName << " does not exist" << std::endl;
            return false;
        }
        // 获取图像信息（不加载数据）
        stbi_info(facePath.getAbsolutePath().c_str(), &w, &h, nullptr);
        if (w != h) {
            std::cerr << "width != height" << std::endl;
            return false;
        }

        size = (size_t)w;

        // 如果有级别前缀，计算Mipmap级别数（log2(size) + 1）
        if (!levelPrefix.empty()) {
            numLevels = (size_t)std::log2(size) + 1;
        }

        // 如果是第0级，创建纹理对象（立方体贴图，R11F_G11F_B10F格式）
        if (level == 0) {
            *texture = Texture::Builder()
                    .width((uint32_t)size)
                    .height((uint32_t)size)
                    .levels((uint8_t)numLevels)
                    .format(Texture::InternalFormat::R11F_G11F_B10F)
                    .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
                    .build(mEngine);
        }
    }

    // RGB_10_11_11_REV encoding: 4 bytes per pixel
    // RGB_10_11_11_REV编码：每个像素4字节
    const size_t faceSize = size * size * sizeof(uint32_t);
    *dim = size;

    // 为6个面分配内存缓冲区（RGB_10_11_11_REV格式）
    Texture::PixelBufferDescriptor buffer(
            malloc(faceSize * 6), faceSize * 6,
            Texture::Format::RGB, Texture::Type::UINT_10F_11F_11F_REV,
            (Texture::PixelBufferDescriptor::Callback) &free);

    bool success = true;
    uint8_t* p = static_cast<uint8_t*>(buffer.buffer);

    // 逐个加载6个面的图像数据
    for (size_t j = 0; j < 6; j++) {
        std::string faceName = levelPrefix + faceSuffix[j] + ".rgb32f";
        Path facePath(Path::concat(path, faceName));
        if (!facePath.exists()) {
            std::cerr << "The face " << faceName << " does not exist" << std::endl;
            success = false;
            break;
        }

        // 加载图像数据（强制加载为RGBA格式，4通道）
        int w, h, n;
        unsigned char* data = stbi_load(facePath.getAbsolutePath().c_str(), &w, &h, &n, 4);
        // 验证尺寸
        if (w != h || w != size) {
            std::cerr << "Face " << faceName << "has a wrong size " << w << " x " << h <<
            ", instead of " << size << " x " << size << std::endl;
            success = false;
            break;
        }

        // 验证数据
        if (data == nullptr || n != 4) {
            std::cerr << "Could not decode face " << faceName << std::endl;
            success = false;
            break;
        }

        // 将数据复制到缓冲区（每个面占用faceSize字节）
        memcpy(p + faceSize * j, data, w * h * sizeof(uint32_t));

        stbi_image_free(data);
    }

    if (!success) return false;

    // 移动缓冲区到输出参数
    *outBuffer = std::move(buffer);

    return true;
}
