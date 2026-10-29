#include "die_shapes.h"
#include "ui.h"
#include <math.h>
#include <algorithm>

using ui::gfx;

namespace {

constexpr int MAX_PTS = 32, MAX_SEGS = 24;

struct Shape {
    float ox[MAX_PTS], oy[MAX_PTS];      // outline (convex)
    int n = 0;
    float sx0[MAX_SEGS], sy0[MAX_SEGS], sx1[MAX_SEGS], sy1[MAX_SEGS];   // facet edges
    int segs = 0;
    void pt(float x, float y) { ox[n] = x; oy[n] = y; n++; }
    void seg(float x0, float y0, float x1, float y1) {
        sx0[segs] = x0; sy0[segs] = y0; sx1[segs] = x1; sy1[segs] = y1; segs++;
    }
};

// Point k of a regular polygon; phase in degrees, -90 = straight up.
inline float px(int k, int n, float r, float phase) { return r * cosf((phase + 360.0f * k / n) * (float)M_PI / 180); }
inline float py(int k, int n, float r, float phase) { return r * sinf((phase + 360.0f * k / n) * (float)M_PI / 180); }

void build(int sides, Shape& s) {
    switch (sides) {
    case 2:   // a coin
        for (int k = 0; k < 28; k++) s.pt(px(k, 28, 1, 0), py(k, 28, 1, 0));
        for (int k = 0; k < 14; k++)
            s.seg(px(k, 14, 0.74f, 0), py(k, 14, 0.74f, 0), px(k + 1, 14, 0.74f, 0), py(k + 1, 14, 0.74f, 0));
        break;
    case 3: {  // a triangle with its corners cut off, inner triangle
        float tx[3], ty[3];
        for (int k = 0; k < 3; k++) { tx[k] = px(k, 3, 1.1f, -90); ty[k] = py(k, 3, 1.1f, -90) + 0.12f; }
        for (int k = 0; k < 3; k++) {
            int j = (k + 1) % 3;
            s.pt(tx[k] + (tx[j] - tx[k]) * 0.2f, ty[k] + (ty[j] - ty[k]) * 0.2f);
            s.pt(tx[k] + (tx[j] - tx[k]) * 0.8f, ty[k] + (ty[j] - ty[k]) * 0.8f);
        }
        for (int k = 0; k < 3; k++)
            s.seg(px(k, 3, 0.5f, -90), py(k, 3, 0.5f, -90) + 0.12f, px(k + 1, 3, 0.5f, -90), py(k + 1, 3, 0.5f, -90) + 0.12f);
        break;
    }
    case 4:   // tetrahedron from above
        for (int k = 0; k < 3; k++) s.pt(px(k, 3, 1.12f, -90), py(k, 3, 1.12f, -90) + 0.16f);
        for (int k = 0; k < 3; k++) s.seg(0, 0.28f, s.ox[k], s.oy[k]);
        break;
    case 6:   // cube on its corner
        for (int k = 0; k < 6; k++) s.pt(px(k, 6, 1, -90), py(k, 6, 1, -90));
        s.seg(0, 0, s.ox[3], s.oy[3]);
        s.seg(0, 0, s.ox[1], s.oy[1]);
        s.seg(0, 0, s.ox[5], s.oy[5]);
        break;
    case 8:   // octahedron, off-centre ridge
        s.pt(0, -1.08f); s.pt(0.82f, 0); s.pt(0, 1.08f); s.pt(-0.82f, 0);
        s.seg(-0.82f, 0, 0.22f, 0.1f); s.seg(0.22f, 0.1f, 0.82f, 0);
        s.seg(0, -1.08f, 0.22f, 0.1f); s.seg(0.22f, 0.1f, 0, 1.08f);
        break;
    case 10:
    case 100:  // a d10 kite
        s.pt(0, -1.08f); s.pt(0.95f, -0.08f); s.pt(0.95f, 0.26f); s.pt(0, 1.0f); s.pt(-0.95f, 0.26f); s.pt(-0.95f, -0.08f);
        s.seg(0, -1.08f, -0.42f, 0.3f); s.seg(-0.42f, 0.3f, 0, 0.56f); s.seg(0, 0.56f, 0.42f, 0.3f); s.seg(0.42f, 0.3f, 0, -1.08f);
        s.seg(0, 0.56f, 0, 1.0f); s.seg(-0.42f, 0.3f, -0.95f, 0.26f); s.seg(0.42f, 0.3f, 0.95f, 0.26f);
        break;
    case 12:  // dodecahedron
        for (int k = 0; k < 10; k++) s.pt(px(k, 10, 1, -90), py(k, 10, 1, -90));
        for (int k = 0; k < 5; k++) {
            s.seg(px(k, 5, 0.56f, -90), py(k, 5, 0.56f, -90), px(k + 1, 5, 0.56f, -90), py(k + 1, 5, 0.56f, -90));
            s.seg(px(k, 5, 0.56f, -90), py(k, 5, 0.56f, -90), s.ox[2 * k], s.oy[2 * k]);
        }
        break;
    default:  // icosahedron (d20, and anything unusual)
        for (int k = 0; k < 6; k++) s.pt(px(k, 6, 1, -90), py(k, 6, 1, -90));
        for (int k = 0; k < 3; k++) {
            float ix = px(k, 3, 0.58f, -90), iy = py(k, 3, 0.58f, -90) + 0.04f;
            s.seg(ix, iy, px(k + 1, 3, 0.58f, -90), py(k + 1, 3, 0.58f, -90) + 0.04f);
            int o = 2 * k;   // outer vertex at the same angle
            s.seg(ix, iy, s.ox[o], s.oy[o]);
            s.seg(ix, iy, s.ox[(o + 1) % 6], s.oy[(o + 1) % 6]);
            s.seg(ix, iy, s.ox[(o + 5) % 6], s.oy[(o + 5) % 6]);
        }
        break;
    }
}

// A thick line as two filled triangles: much quicker than drawWideLine,
// which blends every edge pixel.
void thick(float x0, float y0, float x1, float y1, float w, uint16_t color) {
    if (w < 1.5f) { gfx.drawLine(x0, y0, x1, y1, color); return; }
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) return;
    float nx = -dy / len * w / 2, ny = dx / len * w / 2;
    gfx.fillTriangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, color);
    gfx.fillTriangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, color);
}

}  // namespace

namespace dieshape {

void draw(int sides, float cx, float cy, float r, float rot, float squash, uint16_t fill, uint16_t edge, uint16_t facet,
          float edgeW) {
    Shape s;
    build(sides, s);
    const float c = cosf(rot), sn = sinf(rot);
    auto tx = [&](float x, float y, float& ox, float& oy) {
        x *= squash;   // tumble: squash in the die's own frame, then turn
        ox = cx + (x * c - y * sn) * r;
        oy = cy + (x * sn + y * c) * r;
    };
    float xs[MAX_PTS], ys[MAX_PTS], mx = 0, my = 0;
    for (int i = 0; i < s.n; i++) {
        tx(s.ox[i], s.oy[i], xs[i], ys[i]);
        mx += xs[i];
        my += ys[i];
    }
    mx /= s.n;
    my /= s.n;
    // the outlines are convex: a fan of triangles from the middle fills them
    for (int i = 0; i < s.n; i++) {
        int j = (i + 1) % s.n;
        gfx.fillTriangle(mx, my, xs[i], ys[i], xs[j], ys[j], fill);
    }
    for (int i = 0; i < s.segs; i++) {
        float x0, y0, x1, y1;
        tx(s.sx0[i], s.sy0[i], x0, y0);
        tx(s.sx1[i], s.sy1[i], x1, y1);
        thick(x0, y0, x1, y1, edgeW * 0.6f, facet);
    }
    // the outline, with round corners where its sides meet
    for (int i = 0; i < s.n; i++) {
        int j = (i + 1) % s.n;
        thick(xs[i], ys[i], xs[j], ys[j], edgeW, edge);
        if (edgeW >= 3) gfx.fillCircle(xs[i], ys[i], edgeW / 2, edge);
    }
}

}  // namespace dieshape
