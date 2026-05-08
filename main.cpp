#include <stdio.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <vector>
#include <sstream>

#include <jpeglib.h>
#include <setjmp.h>

#include <tiffio.h>

struct my_error_mgr {
    jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr* my_error_ptr;

// ================= JPEG ERROR =================

METHODDEF(void) my_error_exit(j_common_ptr cinfo) {
    my_error_ptr myerr = (my_error_ptr)cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

// ================= JSON ESCAPE =================

std::string json_escape(const std::string& s) {
    std::ostringstream o;

    for (auto c : s) {
        switch (c) {
        case '"':
            o << "\\\"";
            break;
        case '\\':
            o << "\\\\";
            break;
        case '\b':
            o << "\\b";
            break;
        case '\f':
            o << "\\f";
            break;
        case '\n':
            o << "\\n";
            break;
        case '\r':
            o << "\\r";
            break;
        case '\t':
            o << "\\t";
            break;
        default:
            if ('\x00' <= c && c <= '\x1f') {
                o << "\\u"
                  << std::hex
                  << (int)c;
            }
            else {
                o << c;
            }
        }
    }

    return o.str();
}

// ================= LOWERCASE =================

std::string to_lower(std::string s) {
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) {
            return std::tolower(c);
        }
    );

    return s;
}

// ================= FILE EXISTS =================

bool file_exists(const char* path) {
    FILE* fp = fopen(path, "rb");

    if (!fp)
        return false;

    fclose(fp);
    return true;
}

// ================= JPEG MAGIC =================

bool is_jpeg(FILE* fp) {
    unsigned char buf[2];

    if (fread(buf, 1, 2, fp) != 2)
        return false;

    rewind(fp);

    return buf[0] == 0xFF && buf[1] == 0xD8;
}

// ================= TIFF MAGIC =================

bool is_tiff(FILE* fp) {
    unsigned char buf[4];

    if (fread(buf, 1, 4, fp) != 4)
        return false;

    rewind(fp);

    // II*\0
    if (
        buf[0] == 0x49 &&
        buf[1] == 0x49 &&
        buf[2] == 0x2A &&
        buf[3] == 0x00
    ) {
        return true;
    }

    // MM\0*
    if (
        buf[0] == 0x4D &&
        buf[1] == 0x4D &&
        buf[2] == 0x00 &&
        buf[3] == 0x2A
    ) {
        return true;
    }

    return false;
}

// ================= JPEG CHECK =================

bool check_jpeg(const char* file) {
    FILE* fp = fopen(file, "rb");

    if (!fp)
        return false;

    if (!is_jpeg(fp)) {
        fclose(fp);
        return false;
    }

    jpeg_decompress_struct cinfo;
    my_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return false;
    }

    jpeg_create_decompress(&cinfo);

    jpeg_stdio_src(&cinfo, fp);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return false;
    }

    if (!jpeg_start_decompress(&cinfo)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return false;
    }

    const size_t row_size =
        cinfo.output_width * cinfo.output_components;

    JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)(
        (j_common_ptr)&cinfo,
        JPOOL_IMAGE,
        row_size,
        1
    );

    while (cinfo.output_scanline < cinfo.output_height) {
        if (
            jpeg_read_scanlines(
                &cinfo,
                buffer,
                1
            ) != 1
        ) {
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            fclose(fp);

            return false;
        }
    }

    jpeg_finish_decompress(&cinfo);

    jpeg_destroy_decompress(&cinfo);

    fclose(fp);

    return true;
}

// ================= TIFF CHECK =================

bool check_tiff(const char* file) {
    FILE* fp = fopen(file, "rb");

    if (!fp)
        return false;

    if (!is_tiff(fp)) {
        fclose(fp);
        return false;
    }

    fclose(fp);

    TIFF* tif = TIFFOpen(file, "r");

    if (!tif)
        return false;

    const uint64_t MAX_PIXELS = 20000ULL * 20000ULL;

    do {
        uint32 width = 0;
        uint32 height = 0;

        if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width)) {
            TIFFClose(tif);
            return false;
        }

        if (!TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height)) {
            TIFFClose(tif);
            return false;
        }

        uint64_t pixels =
            (uint64_t)width * (uint64_t)height;

        // 防止超大图爆内存
        if (pixels > MAX_PIXELS) {
            TIFFClose(tif);
            return false;
        }

        std::vector<uint32> raster(pixels);

        if (
            !TIFFReadRGBAImageOriented(
                tif,
                width,
                height,
                raster.data(),
                ORIENTATION_TOPLEFT,
                0
            )
        ) {
            TIFFClose(tif);
            return false;
        }

    } while (TIFFReadDirectory(tif));

    TIFFClose(tif);

    return true;
}

// ================= PRINT JSON =================

void print_json(
    bool ok,
    const std::string& file,
    const std::string& type,
    const std::string& error = ""
) {
    std::cout
        << "{"
        << "\"ok\":" << (ok ? "true" : "false")
        << ",\"file\":\"" << json_escape(file) << "\""
        << ",\"type\":\"" << json_escape(type) << "\"";

    if (!error.empty()) {
        std::cout
            << ",\"error\":\""
            << json_escape(error)
            << "\"";
    }

    std::cout << "}" << std::endl;
}

// ================= MAIN =================

int main(int argc, char** argv) {
    if (argc < 2) {
        print_json(false, "", "", "no_file");
        return 1;
    }

    std::string file = argv[1];

    if (!file_exists(file.c_str())) {
        print_json(false, file, "", "file_not_found");
        return 1;
    }

    std::string ext;

    size_t pos = file.find_last_of('.');

    if (pos != std::string::npos) {
        ext = to_lower(file.substr(pos));
    }

    bool ok = false;
    std::string type;

    if (
        ext == ".jpg" ||
        ext == ".jpeg"
    ) {
        type = "jpeg";
        ok = check_jpeg(file.c_str());
    }
    else if (
        ext == ".tif" ||
        ext == ".tiff"
    ) {
        type = "tiff";
        ok = check_tiff(file.c_str());
    }
    else {
        print_json(
            false,
            file,
            "",
            "unsupported_format"
        );

        return 1;
    }

    if (!ok) {
        print_json(
            false,
            file,
            type,
            "decode_failed"
        );

        return 1;
    }

    print_json(
        true,
        file,
        type
    );

    return 0;
}
