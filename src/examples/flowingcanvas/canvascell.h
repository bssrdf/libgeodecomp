#ifndef _libgeodecomp_examples_flowingcanvas_canvascell_h_
#define _libgeodecomp_examples_flowingcanvas_canvascell_h_

#include <libgeodecomp/misc/floatcoord.h>
#include <libgeodecomp/misc/topologies.h>

#ifndef __host__
#define __host__
#endif

#ifndef __device__
#define __device__
#endif

namespace LibGeoDecomp {

class CanvasCell
{
public:
    class Particle
    {
    public:
        __host__ __device__
        Particle(const float& pos0 = 0, const float& pos1 = 0, const float& _lifetime = 1000) :
            lifetime(_lifetime)
        {
            pos[0] = pos0;
            pos[1] = pos1;
            vel[0] = 0;
            vel[1] = 0;
        }

        __host__ __device__
        void update(const float& deltaT, const float& force0, const float& force1, const float& forceFactor, const float& friction)
        {
            vel[0] += deltaT * forceFactor * force0;
            vel[1] += deltaT * forceFactor * force1;
            vel[0] *= friction;
            vel[1] *= friction;
            // pos[0] += deltaT * vel[0];
            // pos[1] += deltaT * vel[1];
            --lifetime;
        }

        float pos[2];
        float vel[2];
        int lifetime;
    };

    typedef Topologies::Cube<2>::Topology Topology;
    static const int TILE_WIDTH = 4;
    static const int TILE_HEIGHT = 4;
    
    static inline unsigned nanoSteps()
    {
        return 1;
    }

    CanvasCell(
        Coord<2> _pos = Coord<2>(), 
        bool _forceSet = false,
        FloatCoord<2> _forceFixed = FloatCoord<2>()) :
        cameraLevel(0),
        numParticles(0)
    {
        pos[0] = _pos.x();
        pos[1] = _pos.y();
        forceFixed[0] = _forceFixed.c[0];
        forceFixed[1] = _forceFixed.c[1];
        forceSet = _forceSet;
    }

    // fixme: faster conversion if moore-neighborhood is used (instead of von neumann)?
    __host__ __device__
    void update(const CanvasCell *up, const CanvasCell *same, const CanvasCell *down, const unsigned& nanoStep)
    {
        const CanvasCell& oldSelf = *same;

        pos[0] = oldSelf.pos[0];
        pos[1] = oldSelf.pos[1];
        cameraPixel = oldSelf.cameraPixel;
        cameraLevel = oldSelf.cameraLevel;
        numParticles = oldSelf.numParticles;
        for (int i = 0; i < numParticles; ++i) {
            particles[i] = oldSelf.particles[i];
        } 
        // fixme: render particles
        // fixme: spawn particles
        // fixme: move particles to other cells
        // fixme: kill dead particles
        // if (numParticles < 1) {
            // particles[numParticles] = Particle(pos[0], pos[1]);
            // numParticles = 1;
        // }

        forceSet = oldSelf.forceSet;
        if (forceSet) {
            forceFixed[0] = oldSelf.forceFixed[0];
            forceFixed[1] = oldSelf.forceFixed[1];
        } else {
            forceFixed[0] = (up[0].forceFixed[0] +
                             same[-1].forceFixed[0] +
                             same[1].forceFixed[0] +
                             down[0].forceFixed[0]) * 0.25;
            forceFixed[1] = (up[0].forceFixed[1] +
                             same[-1].forceFixed[1] +
                             same[1].forceFixed[1] +
                             down[0].forceFixed[1]) * 0.25;
        }

        cameraLevel = (up[0].cameraLevel +
                             same[-1].cameraLevel +
                             same[1].cameraLevel +
                             down[0].cameraLevel) * 0.25;
        
        float gradientX = same[1].cameraLevel - same[-1].cameraLevel;
        float gradientY = down[0].cameraLevel - up[0].cameraLevel;
        forceVario[0] = 0;
        if ((gradientX > 0.011) || (gradientX < -0.011)) {
            forceVario[0] = 0.01 / gradientX;
        } else {
            forceVario[0] = 0;
        }

        if ((gradientY > 0.011) || (gradientY < -0.011)) {
            forceVario[1] = 0.01 / gradientY;
        } else {
            forceVario[1] = 0;
        }

        forceTotal[0] = 0.5 * (forceFixed[0] + forceVario[0]);
        forceTotal[1] = 0.5 * (forceFixed[1] + forceVario[1]);

        for (int i = 0; i < numParticles; ++i) {
            // Particle& p = particles[i];
            // fixme: parameters
            // p.update(1.0, forceTotal[0], forceTotal[1], 1.0, 0.99);
        }
        
//         float gradient[2];
//         gradient[0] = hood[Coord<2>(1, 0)].smoothCam - hood[Coord<2>(-1, 0)].smoothCam;
//         gradient[1] = hood[Coord<2>(0, 1)].smoothCam - hood[Coord<2>(0, -1)].smoothCam;

//         forceVario[0] = 0;
//         forceVario[1] = 0;
        
//         if ((gradient[0] * gradient[0]) > gradientCutoff) {
//             forceVario[0] = 1.0 / gradient[0];
//         }

//         if ((gradient[1] * gradient[1]) > gradientCutoff) {
//             forceVario[1] = 1.0 / gradient[1];
//         }

//         updateParticles(oldSelf.particle, 
//                         forceVario[0] + forceFixed[0],
//                         forceVario[1] + forceFixed[1]);
//         // moveParticles();
    }

    template<typename COORD_MAP>
    void update(const COORD_MAP& hood, const unsigned& nanoStep)
    {
        update(&hood[Coord<2>(0, -1)], &hood[Coord<2>(0, 0)], &hood[Coord<2>(0, 1)], nanoStep);
    }


    __host__ __device__
    void readCam(const unsigned char& r, const unsigned char& g, const unsigned char& b)
    {
        cameraPixel = (0xff << 24) + (r << 16) + (g <<  8) + (b <<  0);

        int val = (int)r + (int)g + (int)b;
        if (val < 500) {
            cameraLevel = 1.0;
        } else {
            cameraLevel = max(0.0, cameraLevel - 0.05);
        }
    }

    __host__ __device__
    float max(const float& a, const float& b)
    {
        return a > b ? a : b;
    }

    // fixme:
// private:
    unsigned color[TILE_HEIGHT][TILE_WIDTH];
    unsigned cameraPixel;
    float pos[2];
    bool forceSet;

    float cameraLevel;
    float forceVario[2];
    float forceFixed[2];
    float forceTotal[2];
    int numParticles;
    Particle particles[20];  

    void updateParticles()
    {}

    void moveParticles()
    {}
};

}

#endif
