/* pamcmap.h - header file for colormap routines in libppm (alpha enhanced)
 * [from ppmcmap.h, Greg Roelofs, 27 July 1997]
 */

/* Color histogram stuff. */

typedef struct acolorhist_item* acolorhist_vector;
struct acolorhist_item
    {
    apixel acolor;
    int value;
    };

typedef struct acolorhist_list_item* acolorhist_list;
struct acolorhist_list_item
    {
    struct acolorhist_item ch;
    acolorhist_list next;
    };

acolorhist_vector pam_computeacolorhist ARGS(( apixel** apixels, int cols, int rows, int maxacolors, int* acolorsP ));
/* Returns an acolorhist *acolorsP long (with space allocated for maxacolors). */

void pam_addtoacolorhist ARGS(( acolorhist_vector achv, int* acolorsP, int maxacolors, apixel* acolorP, int value, int position ));

void pam_freeacolorhist ARGS(( acolorhist_vector achv ));


/* Color hash table stuff. */

typedef acolorhist_list* acolorhash_table;

acolorhash_table pam_computeacolorhash ARGS(( apixel** apixels, int cols, int rows, int maxacolors, int* acolorsP ));

int pam_lookupacolor ARGS(( acolorhash_table acht, apixel* acolorP ));

acolorhist_vector pam_acolorhashtoacolorhist ARGS(( acolorhash_table acht, int maxacolors ));
acolorhash_table pam_acolorhisttoacolorhash ARGS(( acolorhist_vector achv, int acolors ));

int pam_addtoacolorhash ARGS(( acolorhash_table acht, apixel* acolorP, int value ));
/* Returns -1 on failure. */

acolorhash_table pam_allocacolorhash ARGS(( void ));

void pam_freeacolorhash ARGS(( acolorhash_table acht ));
