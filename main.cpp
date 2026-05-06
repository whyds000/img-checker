#include <stdio.h>
#include <string>
#include <jpeglib.h>
#include <setjmp.h>
#include <tiffio.h>

struct my_error_mgr {
    jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr* my_error_ptr;

METHODDEF(void) my_error_exit(j_common_ptr cinfo) {
    my_error_ptr myerr = (my_error_ptr)cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

bool check_jpeg(const char* file) {
    FILE* fp = fopen(file, "rb");
    if (!fp) return false;

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

    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)(
        (j_common_ptr)&cinfo,
        JPOOL_IMAGE,
        cinfo.output_width * cinfo.output_components,
        1
    );

    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);

    return true;
}

bool check_tiff(const char* file) {
    TIFF* tif = TIFFOpen(file, "r");
    if (!tif) return false;

    do {
        uint32 w, h;
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);

        uint32* buf = (uint32*)_TIFFmalloc(w * h * sizeof(uint32));

        if (!TIFFReadRGBAImage(tif, w, h, buf, 0)) {
            _TIFFfree(buf);
            TIFFClose(tif);
            return false;
        }

        _TIFFfree(buf);

    } while (TIFFReadDirectory(tif));

    TIFFClose(tif);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;

    std::string file = argv[1];
    std::string ext = file.substr(file.find_last_of("."));

    bool ok = false;

    if (ext == ".jpg" || ext == ".jpeg") {
        ok = check_jpeg(file.c_str());
    } else if (ext == ".tif" || ext == ".tiff") {
        ok = check_tiff(file.c_str());
    }

    return ok ? 0 : 1;
}
