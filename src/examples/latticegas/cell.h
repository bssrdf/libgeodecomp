#ifndef _libgeodecomp_examples_latticegas_cell_h_
#define _libgeodecomp_examples_latticegas_cell_h_

#include <iostream>
#include <sstream>

class Cell
{
public:
    // defines for each of the 2^7 flow states which particle moves to
    // which position. stores four variants since the FHP-II model
    // sometimes requires a probabilistic selection. Don't pad to 8
    // bytes to reduce bank conflicts on Nvidia GPUs.
    static char transportTable[128][4][7];
    static unsigned char palette[256][3];

    enum Position { 
        UL, // upper left
        UR, // upper right
        L,  // left
        C,  // center
        R,  // right
        LL, // lower left
        LR  // lower right
    };

    enum State {
        liquid,
        solid,
        source,
        drain
    };

    class Pattern
    {
    public:
        Pattern() :
            flowState(0)
        {
            // default: particle moves back to where it came from (reflection)
            for (int i = 0; i < 7; ++i) {
                destinations[i] = (Position)i;
            }
        }

        /**
         * particle at position src will move to position dst
         */
        void operator()(const Position& src, const Position& dst)
        {
            flowState |= 1 << src;
            int cur = -1;
            for (int i = 0; i < 7; ++i)
                if (destinations[i] == dst)
                    cur = i;
            Position buf = destinations[src];
            destinations[src] = dst;
            destinations[cur] = buf;
        }

        Pattern rotate(unsigned angle) const
        {
            if (angle == 0)
                return *this;

            Pattern p;
            for (int i = 0; i < 7; ++i)
                if (isSet(i))
                    p(successor((Position)i), successor(destinations[i]));
            return p.rotate(angle - 1);
        }

        bool isSet(int i) const
        {
            return (flowState >> i) & 1;
        }

        const Position& getDest(int i) const
        {
            return destinations[i];
        }

        static std::string posToString(const Position& pos)
        {
            switch (pos) {
            case UL:
                return "UL";
            case UR:
                return "UR";
            case L:
                return "L";
            case R:
                return "R";
            case LL:
                return "LL";
            case LR:
                return "LR";
            default:
                return "C";
            }            
        }

        static Position successor(const Position& pos)
        {
            switch (pos) {
            case UL:
                return UR;
            case UR:
                return R;
            case L:
                return UL;
            case R:
                return LR;
            case LL:
                return L;
            case LR:
                return LL;
            default:
                return C;
            }
        }

        int getFlowState() const
        {
            return flowState;
        }
        
        std::string toString() const
        {
            std::ostringstream buf;
            buf << "(flow: " << flowState << ", ";
            for (int i = 0; i < 7; ++i) {
                if (isSet(i)) {
                    buf << posToString((Position)i) << "->" << posToString(destinations[i]) << ", ";
                }
            }
            buf << ")";
            return buf.str();
        }

    private:
        int flowState;
        Position destinations[7];
    };
    
    inline Cell(const State& newState=liquid, const int& k=-1, const int& val=1) :
        state(newState)
    {
        for (int i = 0; i < 7; ++i)
            particles[i] = 0;
        if (k >= 0)
            particles[k] = val;
    }

    inline void update(
        const int& randSeed,
        const char& oldState,
        const char& ul, 
        const char& ur, 
        const char& l, 
        const char& c, 
        const char& r,
        const char& ll,
        const char& lr)
    {
        state = oldState;

        if (oldState == liquid) {
            int flowState = 
                (not0(ul) << 6) +
                (not0(ur) << 5) +
                (not0(l)  << 4) +
                (not0(c)  << 3) +
                (not0(r)  << 2) +
                (not0(ll) << 1) +
                (not0(lr) << 0);

            int rand = ((randSeed ^ flowState) >> 3) & 3;

            particles[(int)transportTable[flowState][rand][0]] = ul;
            particles[(int)transportTable[flowState][rand][1]] = ur;
            particles[(int)transportTable[flowState][rand][2]] =  l;
            particles[(int)transportTable[flowState][rand][3]] =  c;
            particles[(int)transportTable[flowState][rand][4]] =  r;
            particles[(int)transportTable[flowState][rand][5]] = ll;
            particles[(int)transportTable[flowState][rand][6]] = lr;
            return;
        }

        particles[UL] = ul;
        particles[UR] = ur;
        particles[L ] = l;
        particles[C ] = c;
        particles[R ] = r;
        particles[LL] = ll;
        particles[LR] = lr;
    } 

    inline char& getState() 
    {
        return state;
    }

    inline const char& getState() const
    {
        return state;
    }

    inline const char& operator[](const int& i) const
    {
        return particles[i];
    }

    inline std::string toString() const
    {
        std::ostringstream buf;
        buf << "(" 
            << stateToString(state) << ", "
            << particleToString(particles[0]) << ", "
            << particleToString(particles[1]) << ", "
            << particleToString(particles[2]) << ", "
            << particleToString(particles[3]) << ", "
            << particleToString(particles[4]) << ", "
            << particleToString(particles[5]) << ", "
            << particleToString(particles[6]) << ")";
        return buf.str();
    }

    static void initTransportTable() 
    {
        Cell::Pattern p;
        p(L, UL);
        p(R, LR);
        std::cout << "L: " << L << "\n"
                  << "R: " << R << "\n"
                  << "UL: " << UL << "\n"
                  << "LR: " << LR << "\n"
                  << "p.isSet(L) = " << p.isSet(L) << "\n";
        std::cout << "\n" << p.toString() << "\n";
        std::cout << "\n" << p.rotate(1).toString() << "\n";
        std::cout << "\n" << p.rotate(2).toString() << "\n";
        std::cout << "\n" << p.rotate(6).toString() << "\n";

        // default: no collision
        for (int i = 0; i < 128; ++i) 
            for (int rand = 0; rand < 4; ++rand)
                fillIn(i, rand, LR, LL, R, C, L, UR, UL);

        // add patterns according to Fig. 7.2:
        {
            // a
            Pattern p[2];
            p[0](L, LR);
            p[0](R, UL);
            p[1](L, UR);
            p[1](R, LL);
            addPattern(p, 2);
        }
        {
            // b
            Pattern p[0];
            p[0](UL, UL);
            p[0](R,  R);
            p[0](LL, LL);
            addPattern(p, 1);
        }
        {
            // c
            // fixme
            // Pattern p[2];
            // p[0](L, UR);
            // p[0](C, LR);
            // p[0](L, LR);
            // p[0](C, UR);
            // addPattern(p, 2);
        }

    }

    static void initPalette() {
        palette[0][0] = 0;
        palette[0][1] = 0;
        palette[0][2] = 0;
        palette[1][0] = 255;
        palette[1][1] = 0;
        palette[1][2] = 0;
        for (int i = 2; i < 256; ++i) {
            palette[i][0] = 0;
            palette[i][1] = 255;
            palette[i][2] = 255;
        }
    }

    static void addPattern(const Pattern *p, const int& num)
    {
        for (int angle = 0; angle < 6; ++angle) {
            for (int offset = 0; offset < num; ++offset) {
                Pattern rot = p[offset].rotate(angle);
                addFinalPattern(offset, rot);
                if (num == 2) {
                    addFinalPattern(offset + 2, rot);
                }
                if (num == 1) {
                    addFinalPattern(offset + 1, rot);
                    addFinalPattern(offset + 2, rot);
                    addFinalPattern(offset + 3, rot);
                }
            }
        }
    }

    static void addFinalPattern(const int& offset, const Pattern& p)
    {
        for (int i = 0; i < 7; ++i)
            transportTable[p.getFlowState()][offset][i] = p.getDest(i);
    } 
    
    char particles[7];
    char state;

private:
    inline bool not0(const char& c) const
    {
        return c != 0? 1 : 0;
    }

    inline std::string particleToString(const char& p) const
    {
        if (p == 0)
            return " ";
        if (p == 1)
            return "r";
        if (p == 2)
            return "g";
        if (p == 3)
            return "b";
        if (p == 4)
            return "y";
        return "X";
    }

    inline std::string stateToString(const char& state) const
    {
        switch (state) {
        case liquid:
            return "l";
        case solid:
            return "S";
        case source:
            return "s";
        case drain:
            return "d";
        default:
            return "X";
        }
    }

    static void fillIn(const int& flowState,
                       const int& rand,
                       const char& ul,
                       const char& ur,
                       const char& l,
                       const char& c,
                       const char& r,
                       const char& ll,
                       const char& lr)
    {
        transportTable[flowState][rand][UL] = ul;
        transportTable[flowState][rand][UR] = ur;
        transportTable[flowState][rand][L]  = l;
        transportTable[flowState][rand][C]  = c;
        transportTable[flowState][rand][R]  = r;
        transportTable[flowState][rand][LL] = ll;
        transportTable[flowState][rand][LR] = lr;
    }
};

#endif
