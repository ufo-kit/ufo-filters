#ifndef UFO_RING_COORDINATES_H
#define UFO_RING_COORDINATES_H

struct _UfoRingCoordinate {
    float x;
    float y;
    float r;
    float contrast;
    float intensity;
};

struct _UfoRingCoordinatesStream {
    float nb_elt;
    struct _UfoRingCoordinate *coord;
};

typedef struct _UfoRingCoordinate UfoRingCoordinate;
typedef struct _UfoRingCoordinatesStream URCS;

#endif
