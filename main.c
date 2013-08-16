#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <gd.h>
#include <gdfontg.h>


const char program[] = "rdf_plot";
const char author[]  = "Petr Voytsik";
const char version[] = "1.0";


#define handle_error(msg) \
        do { perror(msg); exit(EXIT_FAILURE); } while (0)


#define FRAME_SIZE 40000UL
static const uint64_t syn64 = 0xFA463E11034CC457;
static const long shift = 60;

struct rdf_info{
    char sig[5];
    char date[18];
    char terminal[11];
    char source[11];
    char exper[11];
    time_t time0;
    char name[128];
};


static void fill_rdf_info(const char *data, struct rdf_info *info)
{
    struct tm date;

    memset(info, 0, sizeof(struct rdf_info));

    puts("********************");

    strncpy(info->sig, data, 5);
    info->sig[4] = 0;
    puts(info->sig);
    
    strncpy(info->date, &data[6], 18);
    info->date[17] = 0;
    puts(info->date);

    strncpy(info->terminal, &data[24], 11);
    info->terminal[10] = 0;
    puts(info->terminal);

    strncpy(info->source, &data[35], 11);
    info->source[10] = 0;
    puts(info->source);

    strncpy(info->exper, &data[46], 11);
    info->exper[10] = 0;
    puts(info->exper);

    puts("********************");

    memset(&date, 0, sizeof(struct tm));
    strptime(info->date, "%Y %j-%T", &date);
    info->time0 = mktime(&date);
}

static void save_png(gdImagePtr im, const char *filename)
{
    FILE *fp;
    
    fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Can't save png image %s\n", filename);
        return;
    }
    gdImagePng(im, fp);
    fclose(fp);
}

static void write_row(gdImagePtr im, int row, const char *data, unsigned len)
{
    unsigned i, j;
    int col = 0;

    for(i = 0; i < len; i++)
        for(j = 0; j < 8; j++){
            if(data[i] & (1 << j))
                gdImageSetPixel(im, col, row, 0x000FF00);
            col++;
        }
}

static void print_string(gdImagePtr im, int x, int y, const char *str)
{
    gdFontPtr font = gdFontGetGiant();

    x -= (10 + strlen(str)*font->w);
    gdImageFilledRectangle(im, x-1, y, 
                           x+strlen(str)*font->w, 
                           y+font->h, 0x0);
    gdImageString(im, font, x, y, (unsigned char *)str, 0x00FFFFFF);

}

static int print_800frames(const char *frame, size_t offset, const char *end, 
                           const struct rdf_info *file_info)
{
    static unsigned n = 0;
    unsigned sec, i;
    int ret = 0;
    const unsigned w = 1200, h = 800;
    time_t time = 0;
    char time_str[64], sec_str[16];
    char img_fname[128];
    gdImagePtr im;

    im = gdImageCreateTrueColor(w, h);
    gdImageFilledRectangle(im, 0, 0, w-1, h-1, 0x0);
    sec = offset / (40000*400);
    printf("Save picture #%u: begin at %lu (%lu frame, %u sec)\n", 
            n++, offset, offset / 40000, sec);
    for(i = 0; i < h; i++){
        write_row(im, i, frame, w/8);
        frame += FRAME_SIZE;
        if(frame > end){
            printf("End of file\n");
            ret++;
            break;
        }
    }
    
    sprintf(img_fname, "%s_%03d.png", file_info->name, sec);
    print_string(im, w, 10, file_info->exper);
    print_string(im, w, 30, file_info->terminal);
    time = file_info->time0 + sec;
    ctime_r(&time, time_str);
    time_str[strlen(time_str)-1] = 0;
    print_string(im, w, 50, time_str);
    sprintf(sec_str, "%02d:%02d", sec/60, sec%60);
    print_string(im, w, 70, sec_str);
    save_png(im, img_fname);
    gdImageDestroy(im);

    return ret;
}

static unsigned int bit_count64(uint64_t qword)
{
    unsigned int c = 0;

    for (c = 0; qword; c++)
        qword &= qword - 1; // clear the least significant bit set

    return c;
}

static unsigned int sync_cmp(const char *data)
{
    return bit_count64( *(uint64_t *)data ^ syn64 );
}

static const char * find_sync(const char *data)
{
    const char *p = NULL;
    unsigned int i, c;

    data += shift;

    for(i = 0; i < 100*FRAME_SIZE; i++){
        c = sync_cmp(&data[i]);
        if(c <= 10){
            p = &data[i];
            printf("Found sync with %u wrong bits at 0x%lX\n", c, p-data);
            p -= shift;
            break;
        }
    }
    
    if(!p)
        printf("Could not find sync!\n");

    return p;
}

void work(const char *data, size_t len, unsigned start, 
          unsigned interv, unsigned num, const char *file_name, int z)
{
    unsigned i, n = 0, sec = 0;
    const char *data_end = data + len;
    const char *frame, *new_frame = NULL; 
    char *p;
    char nulls2[2] = {0, 0};
    struct rdf_info file_info;

    /* At least 100 frames */
    if(len < 100*FRAME_SIZE + 0x100){
        fprintf(stderr, "File too short, exiting.\n");
        return;
    }

    if(start * FRAME_SIZE * 400 > len){
        fprintf(stderr, "Start after end of file\n");
        return;
    }

    memset(&file_info, 0, sizeof(struct rdf_info));
    /* Get information about file */
    if(strncmp(data, "RDF1", 5) == 0)
        fill_rdf_info(data, &file_info);
    
    strncpy(file_info.name, file_name, 128);
    p = strrchr(file_info.name, '.');
    if(p)
        *p = 0;

    frame = find_sync(data + start * FRAME_SIZE * 400);

    if(!frame){
        fprintf(stderr, "Can not find first frame\n");
        return;
    }

    printf("First frame offset = 0x%lX\n", frame-data);
    /*frame += start * FRAME_SIZE * 400;*/

    do{
        if(print_800frames(frame, frame-data, data_end, &file_info))
            break;

        frame += FRAME_SIZE * 400 * interv;
        if(++n >= num){
            printf("Stop!\n");
            break;
        }
        if(frame > data_end){
            printf("End of file\n");
            break;
        }

        if(sync_cmp(frame + shift) >= 10 && z){
            new_frame = find_sync(frame+shift);
            if(!new_frame){
                printf("Try to find something else...\n");
                new_frame = frame;
                for(i = 0; i < 50; i++){
                    new_frame = memmem(new_frame, 2*FRAME_SIZE, 
                                       nulls2, 2);
                    if(!new_frame)
                        break;
                    if(!memcmp(new_frame, new_frame+FRAME_SIZE, 2) && 
                       !memcmp(new_frame, new_frame+2*FRAME_SIZE, 2)){
                        puts("+");
                        break;
                    }
                    new_frame += 2;
                
                }
                if(!new_frame){
                    sec = (frame-data) / (40000*400);
                    printf("Can not find valid frame near second %u ", sec);
                    printf("Plot as is\n");

                    continue;
                }
                frame = new_frame - (shift+20);
            }else
                frame = new_frame;
        }else{
            printf("Ok!\n");
        }
    }while(frame < data_end);
}

static void usage()
{
    printf("Usage: %s [-i interval] [-n number_of_im] [-s start_from] "
           "[-z] <FILE>\n", program);
    printf("  -i \t interal between images in seconds (default = 60s)\n");
    printf("  -n \t maximum number of images (default = 30)\n");
    printf("  -s \t start from second (default = 0)\n");
    printf("  -z \t do not try to find next syncro-code\n");
}

int main(int argc, char *argv[])
{
    char *ptr;
    int fd;
    struct stat sb;
    size_t length;
    /* Program options */
    int opt;
    unsigned interval = 60;  /* Interval between images (sec) */
    unsigned max_number = 30; /* Maximum number of images */
    unsigned start_sec = 0;
    char file_name[128];
    int z = 1;

    while ((opt = getopt(argc, argv, "hi:n:s:z")) != -1){
        switch (opt) {
            case 'i':
                interval = atoi(optarg);
                break;
            case 'n':
                max_number = atoi(optarg);
                break;
            case 's':
                start_sec = atoi(optarg);
                break;
            case 'z':
                z = 0;
                break;
            case 'h':
                usage();
                return EXIT_SUCCESS;
                break;
            default:
                usage();
                return EXIT_FAILURE;
        }
    }

    if(optind >= argc){
        usage();
        return EXIT_FAILURE;
    }

    strncpy(file_name, argv[optind], 128);

    fd = open(file_name, O_RDONLY);
    if(fd < 0)
        handle_error("open");

    if (fstat(fd, &sb) < -1)
        handle_error("fstat");

    length = sb.st_size;
    /*fprintf(stderr, "File \"%s\" size: %lu\n", file_name, length);*/

    ptr = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0L);
    if (ptr == MAP_FAILED)
        handle_error("mmap");

    work(ptr, length, start_sec, interval, max_number, basename(file_name), z);

    munmap(ptr, length);
    close(fd);

    return 0;
}
