#include "battlespades/network/workshop_preview.hpp"

// Private decoder symbols avoid interfering with bimg's or the editor's stb code.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 4096
#include <stb_image.h>
#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

#include <algorithm>
#include <memory>

namespace battlespades::network {
std::vector<std::uint8_t> normalize_workshop_preview(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size()>2U*1024U*1024U) return {};
    int width{},height{},channels{};
    const auto size=static_cast<int>(bytes.size());
    if (!stbi_info_from_memory(bytes.data(),size,&width,&height,&channels) || width<=0 || height<=0 ||
        width>4096 || height>4096 || static_cast<std::uint64_t>(width)*static_cast<unsigned>(height)>8U*1024U*1024U) return {};
    const std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> decoded{
        stbi_load_from_memory(bytes.data(),size,&width,&height,&channels,4),stbi_image_free};
    if (!decoded) return {};
    const auto scale=std::min({1.0,640.0/width,360.0/height});
    const auto target_width=std::max(1,static_cast<int>(width*scale));
    const auto target_height=std::max(1,static_cast<int>(height*scale));
    std::vector<std::uint8_t> resized(static_cast<std::size_t>(target_width)*static_cast<std::size_t>(target_height)*4U);
    if (!stbir_resize_uint8_linear(decoded.get(),width,height,0,resized.data(),target_width,target_height,0,STBIR_RGBA)) return {};
    std::vector<std::uint8_t> canvas(640U*360U*3U);
    constexpr std::uint8_t background[]{25,26,20};
    for (std::size_t i=0;i<canvas.size();++i) canvas[i]=background[i%3U];
    for (int y=0;y<target_height;++y) for (int x=0;x<target_width;++x) {
        const auto input=static_cast<std::size_t>(y*target_width+x)*4U;
        const auto output=static_cast<std::size_t>((y+(360-target_height)/2)*640+x+(640-target_width)/2)*3U;
        const auto alpha=static_cast<unsigned>(resized[input+3U]);
        for (std::size_t c=0;c<3U;++c)
            canvas[output+c]=static_cast<std::uint8_t>((resized[input+c]*alpha+background[c]*(255U-alpha)+127U)/255U);
    }
    struct Sink { std::vector<std::uint8_t> data; bool failed{}; } sink;
    const auto write=[](void* context,void* data,int count) noexcept {
        auto& output=*static_cast<Sink*>(context);
        if (output.failed || count<=0) return;
        const auto* first=static_cast<const std::uint8_t*>(data);
        try { output.data.insert(output.data.end(),first,first+count); } catch (...) { output.failed=true; }
    };
    if (!stbi_write_png_to_func(write,&sink,640,360,3,canvas.data(),640*3) || sink.failed) return {};
    return std::move(sink.data);
}
}
