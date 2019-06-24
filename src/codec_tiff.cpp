#include "codec_tiff.h"

#include <optional>

#include "tiffio.h"

#include "codec_jpeg2000.h"

// TIFFImage::TIFFImage(std::filesystem::path const& path): Image{path} {
//     if (!path::ends_with(path, {".tiff", ".tif"}))
//         throw std::runtime_error{"unsupported extension"};
//     TIFFSetDirectory(tiff_, 0);
//     float spacing_y;
//     float spacing_x;
//     if (TIFFGetField(tiff_, TIFFTAG_YRESOLUTION, &spacing_y) == 1)
//         spacing_.push_back(10000. / spacing_y);
//     if (TIFFGetField(tiff_, TIFFTAG_XRESOLUTION, &spacing_x) == 1)
//         spacing_.push_back(10000. / spacing_x);
// }

namespace al::codec::tiff {
namespace detail {

DType get_dtype(const File& f) {
    auto bitdepth = f.get<uint16_t>(TIFFTAG_BITSPERSAMPLE);
    auto dtype = f.try_get<uint16_t>(TIFFTAG_SAMPLEFORMAT).value_or(1);

    switch (dtype) {
    case SAMPLEFORMAT_IEEEFP:
        switch (bitdepth) {
        case 32:
            return DType::Float;
        default:
            throw std::runtime_error{"Unsupported bitdepth"
                                     + std::to_string(bitdepth)};
        }
    case SAMPLEFORMAT_UINT:
        switch (bitdepth) {
        case 32:
            return DType::UInt32;
        case 16:
            return DType::UInt16;
        case 8:
            return DType::UInt8;
        default:
            throw std::runtime_error{"Unsupported bitdepth"
                                     + std::to_string(bitdepth)};
        }
    default:
        throw std::runtime_error{"Unsupported dtype"};
    }
}

Color get_ctype(const File& f) {
    auto ctype = f.get<uint16_t>(TIFFTAG_PHOTOMETRIC);
    auto samples = (ctype != PHOTOMETRIC_YCBCR)
        ? f.get<uint16_t>(TIFFTAG_SAMPLESPERPIXEL)
        : 4;

    switch (ctype) {
    case PHOTOMETRIC_MINISBLACK:
        return (samples == 1) ? Color::Monochrome : Color::Indexed;
    case PHOTOMETRIC_RGB:
        switch (samples) {
        case 3:
            return Color::RGB;
        case 4:
            return Color::ARGB;
        default:
            throw std::runtime_error{"Unsupported samples: "
                                     + std::to_string(samples)};
        }
    case PHOTOMETRIC_YCBCR:
        if (samples != 4)
            throw std::runtime_error{"Invalid color"};
        return Color::ARGB;
    default:
        throw std::runtime_error{"Unsupported color type"};
    }
}

auto read_pyramid(File const& f) {
    TIFFSetDirectory(f, 0);
    uint16_t level_count = TIFFNumberOfDirectories(f);
    if (level_count < 1)
        throw std::runtime_error{"Tiff have no levels"};

    std::vector<Level> levels;
    for (uint16_t level = 0; level < level_count; ++level) {
        TIFFSetDirectory(f, level);

        if (!TIFFIsTiled(f))
            continue;

        Level lv{Shape{f.get<uint32_t>(TIFFTAG_IMAGELENGTH),
                       f.get<uint32_t>(TIFFTAG_IMAGEWIDTH)},
                 Shape{f.get<uint32_t>(TIFFTAG_TILELENGTH),
                       f.get<uint32_t>(TIFFTAG_TILEWIDTH)}};
        levels.push_back(lv);
    }
    TIFFSetDirectory(f, 0);
    return levels;
}

} // namespace detail

Tiff::Tiff(Path const& path)
  : Codec<Tiff>{}
  , file_{path, "rm"}
  , dtype_{detail::get_dtype(file_)}
  , ctype_{detail::get_ctype(file_)}
  , codec_{file_.get<uint16_t>(TIFFTAG_COMPRESSION)}
{
    auto c_descr = file_.get_defaulted<const char*>(TIFFTAG_IMAGEDESCRIPTION);
    if (c_descr) {
        std::string descr{c_descr.value()};
        if (descr.find("DICOM") != std::string::npos
                || descr.find("xml") != std::string::npos
                || descr.find("XML") != std::string::npos)
            throw std::runtime_error{"Unsupported format: " + descr};
    }
    if (!TIFFIsTiled(file_))
        throw std::runtime_error{"Tiff is not tiled"};
    if (file_.get<uint16_t>(TIFFTAG_PLANARCONFIG) != PLANARCONFIG_CONTIG)
        throw std::runtime_error{"Tiff is not contiguous"};

    levels = detail::read_pyramid(file_);
    samples = al::to_samples(ctype_);
}

DType Tiff::dtype() const noexcept { return dtype_; }

} // namespace al::codec::tiff
