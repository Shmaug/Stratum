//#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E resolve

#include "smallpssmlt.h"

float random(unsigned int *rng) {
    *rng = (1103515245 * (*rng) + 12345);
    return (float) *rng / (float) 0xFFFFFFFF;
}

Sphere spheres[] = {//Scene: radius, position, emission, color, material
	{ 1e5, Vector3(1e5 + 1, 40.8, 81.6), Vector3(), Vector3(.75, .25, .25), DIFF },//Left
	{ 1e5, Vector3(-1e5 + 99, 40.8, 81.6), Vector3(), Vector3(.25, .25, .75), DIFF },//Rght
	{ 1e5, Vector3(50, 40.8, 1e5), Vector3(), Vector3(.75, .75, .75), DIFF },//Back
	{ 1e5, Vector3(50, 40.8, -1e5 + 170), Vector3(), Vector3(), DIFF },//Frnt
	{ 1e5, Vector3(50, 1e5, 81.6), Vector3(), Vector3(.75, .75, .75), DIFF },//Botm
	{ 1e5, Vector3(50, -1e5 + 81.6, 81.6), Vector3(), Vector3(.75, .75, .75), DIFF },//Top
	{ 16.5, Vector3(27, 16.5, 47), Vector3(), Vector3(1, 1, 1) * .999, SPEC },//Mirr
	{ 16.5, Vector3(73, 16.5, 78), Vector3(), Vector3(1, 1, 1) * .999, REFR },//Glas
	{ 600, Vector3(50, 681.6 - .27, 81.6), Vector3(12, 12, 12), Vector3(), DIFF } //Lite
};

inline Real removeNaN(Real x) { return isnan(x) || x < 0.0 ? 0.0 : x; }

inline int toInt(Real x) { return int(pow(clamp(x), 1 / 2.2) * 255 + .5); }

inline bool intersect(const Ray &r, Real &t, int &id) {
    Real n = sizeof(spheres) / sizeof(Sphere), d, inf = t = 1e20;
    for (int i = int(n); i--;)
        if ((d = spheres[i].intersect(r)) && d < t) {
            t = d;
            id = i;
        }
    return t < inf;
}

const Real largeStepProb = 0.25;

struct RadianceRecord {
    int x, y;
    Vector3 Li;

    RadianceRecord() {
        x = y = 0;
    };
};

struct Sampler {
    unsigned int seed;
    std::vector<PrimarySample> X;
    uint64_t currentIteration = 0;
    bool largeStep = true;
    uint64_t lastLargeStepIteration = 0;
    int w, h;
    RadianceRecord current;

    Sampler(int w, int h, unsigned int seed) : w(w), h(h), seed(seed) {}

    uint32_t sampleIndex = 0;
    uint64_t a = 0, r = 0;

    void startIteration() {
        sampleIndex = 0;
        currentIteration++;
        largeStep = uniform() < largeStepProb;
    }

    Real uniform() {
        return random(&seed);
    }

    void mutate(PrimarySample &Xi, int sampleIndex) {
        Real s1, s2;
        if (sampleIndex >= 2) {
            s1 = 1.0 / 1024.0, s2 = 1.0 / 64.0;
        } else if (sampleIndex == 1) {
            s1 = 1.0 / h, s2 = 0.1;
        } else {
            s1 = 1.0 / w, s2 = 0.1;
        }
        if (Xi.lastModificationIteration < lastLargeStepIteration) {
            Xi.value = uniform();
            Xi.lastModificationIteration = lastLargeStepIteration;
        }

        if (largeStep) {
            Xi.backup();
            Xi.value = uniform();
        } else {
            int64_t nSmall = currentIteration - Xi.lastModificationIteration;

            auto nSmallMinus = nSmall - 1;
            if (nSmallMinus > 0) {
                auto x = Xi.value;
                while (nSmallMinus > 0) {
                    nSmallMinus--;
                    x = mutate(x, s1, s2);
                }
                Xi.value = x;
                Xi.lastModificationIteration = currentIteration - 1;
            }
            Xi.backup();
            Xi.value = mutate(Xi.value, s1, s2);
        }

        Xi.lastModificationIteration = currentIteration;
    }

    Real next() {
        if (sampleIndex >= X.size()) {
            X.resize(sampleIndex + 1u);
        }
        auto &Xi = X[sampleIndex];
        mutate(Xi, sampleIndex);
        sampleIndex += 1;
        return Xi.value;

    }

    Real mutate(Real x, Real s1, Real s2) {
        Real r = uniform();
        if (r < 0.5) {
            r = r * 2.0;
            x = x + s2 * exp(-log(s2 / s1) * r);
            if (x > 1.0) x -= 1.0;
        } else {
            r = (r - 0.5) * 2.0;
            x = x - s2 * exp(-log(s2 / s1) * r);
            if (x < 0.0) x += 1.0;
        }
        return x;
    }

    void accept() {
        if (largeStep) {
            lastLargeStepIteration = currentIteration;
        }
        a++;
    }

    void reject() {
        for (PrimarySample &Xi :X) {
            if (Xi.lastModificationIteration == currentIteration) {
                Xi.restore();
            }
        }
        r++;
        --currentIteration;
    }
};

Vector3 radiance(Ray r, ARG_INOUT(Sampler) sampler) {
    Real t;              // distance to intersection
    int id = 0;          // id of intersected object
    Vector3 cl(0, 0, 0); // accumulated color
    Vector3 cf(1, 1, 1); // accumulated reflectance
    int depth = 0;
    while (true) {
        Real u1 = sampler.next(), u2 = sampler.next(), u3 = sampler.next();
        if (!intersect(r, t, id)) return cl; // if miss, return black

        const Sphere &obj = spheres[id];     // the hit object

        Vector3 x = r.o + r.d * t, n = normalize(x - obj.p), nl = dot(n, r.d) < 0 ? n : n * -1, f = obj.c;

        Real p = f.x > f.y && f.x > f.z ? f.x : f.y > f.z ? f.y : f.z; // max refl

        cl = cl + cf.mult(obj.e);
        if (++depth > 5)
			if (u3 < p)
				f = f * (1 / p);
			else {
				return cl;
			} //R.R.

        cf = cf.mult(f);
        if (obj.refl == DIFF) {                  // Ideal DIFFUSE reflection
            Real r1 = 2 * M_PI * u1, r2 = u2, r2s = sqrt(r2);
            Vector3 w = nl, u = normalize((fabs(w.x) > .1 ? Vector3(0, 1) : Vector3(1)) % w), v = w % u;
            Vector3 d = normalize(u * cos(r1) * r2s + v * sin(r1) * r2s + w * sqrt(1 - r2));
            //return obj.e + f.mult(radiance(Ray(x,d),depth,Xi));
            r = Ray(x, d);
            continue;
        } else if (obj.refl == SPEC) {           // Ideal SPECULAR reflection
            r = Ray(x, r.d - n * 2 * dot(n, r.d));
            continue;
        }

        Ray reflRay(x, r.d - n * 2 * dot(n, r.d)); // Ideal dielectric REFRACTION
        bool into = dot(n, nl) > 0;                // Ray from outside going in?
        Real nc = 1, nt = 1.5, nnt = into ? nc / nt : nt / nc, ddn = dot(r.d, nl), cos2t;
        if ((cos2t = 1 - nnt * nnt * (1 - ddn * ddn)) < 0) {    // Total internal reflection
            //return obj.e + f.mult(radiance(reflRay,depth,Xi));
            r = reflRay;
            continue;
        }

        Vector3 tdir = normalize((r.d * nnt - n * ((into ? 1 : -1) * (ddn * nnt + sqrt(cos2t)))));
        Real a = nt - nc, b = nt + nc, R0 = a * a / (b * b), c = 1 - (into ? -ddn : dot(tdir, n));
        Real Re = R0 + (1 - R0) * c * c * c * c * c, Tr = 1 - Re, P = .25 + .5 * Re, RP = Re / P, TP = Tr / (1 - P);
        // return obj.e + f.mult(sampler.next()<P ?
        //                       radiance(reflRay,    depth,Xi)*RP:
        //                       radiance(Ray(x,tdir),depth,Xi)*TP);

        if (u1 < P) {
            cf = cf * RP;
            r = reflRay;
        } else {
            cf = cf * TP;
            r = { x, tdir };
        }
    }
}

Vector3 radiance(int x, int y, int w, int h, ARG_INOUT(Sampler) sampler) {
    Ray cam(Vector3(50, 52, 295.6), Vector3(0, -0.042612, -1).norm()); // cam pos, dir
    Vector3 cx = Vector3(w * .5135 / h), cy = normalize(cx % cam.d) * .5135;
    Real r1 = 2 * sampler.next(), dx = r1 < 1 ? sqrt(r1) - 1 : 1 - sqrt(2 - r1);
    Real r2 = 2 * sampler.next(), dy = r2 < 1 ? sqrt(r2) - 1 : 1 - sqrt(2 - r2);
    Vector3 d = cx * (((1 + dx) / 2 + x) / w - .5) + cy * (((1 + dy) / 2 + y) / h - .5) + cam.d;
    return radiance(Ray(cam.o + d * 140, normalize(d)), sampler);
}


RadianceRecord radiance(Sampler &sampler, bool bootstrap) {
    if (!bootstrap)
        sampler.startIteration();
    Real x = sampler.next();
    Real y = sampler.next();
    RadianceRecord record;
    record.x = std::min<int>(sampler.w - 1, lround(x * sampler.w));
    record.y = std::min<int>(sampler.h - 1, lround(y * sampler.h));
    record.Li = radiance(record.x, record.y, sampler.w, sampler.h, sampler);
    return record;
}

Real b;

Real ScalarContributionFunction(const Vector3 &Li) {
    return 0.2126 * Li.x + 0.7152 * Li.y + 0.0722 * Li.z;
}

void RunMarkovChain(Sampler &sampler, RadianceRecord &r1, RadianceRecord &r2) {
    auto r = radiance(sampler, false);
    Real accept = std::max(0.0,
                             std::min(1.0,
                                      ScalarContributionFunction(r.Li) /
                                      ScalarContributionFunction(sampler.current.Li)));
    Real weight1 = (accept + (sampler.largeStep ? 1.0 : 0.0))
                     / (ScalarContributionFunction(r.Li) / b + largeStepProb);
    Real weight2 = (1 - accept)
                     / (ScalarContributionFunction(sampler.current.Li) / b + largeStepProb);
    r1.x = r.x;
    r1.y = r.y;
    r1.Li = r.Li * weight1;
    r2.x = sampler.current.x;
    r2.y = sampler.current.y;
    r2.Li = sampler.current.Li * weight2;
    if (accept == 1 || sampler.uniform() < accept) {
        sampler.accept();
        sampler.current = r;
    } else {
        sampler.reject();
    }
}


uint32_t nBootstrap = 100000;

inline uint64_t floatToBits(Real f) {
    uint64_t ui;
    memcpy(&ui, &f, sizeof(Real));
    return ui;
}

inline Real bitsToFloat(uint64_t ui) {
    Real f;
    memcpy(&f, &ui, sizeof(uint64_t));
    return f;
}

class AtomicFloat {
public:
    AtomicFloat(Real v = 0) { bits = floatToBits(v); }

    AtomicFloat(const AtomicFloat &rhs) {
        bits.store(rhs.bits.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    operator Real() const { return bitsToFloat(bits); }

    Real operator=(Real v) {
        bits = floatToBits(v);
        return v;
    }

    AtomicFloat &operator=(const AtomicFloat &rhs) {
        bits.store(rhs.bits.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }

    void add(Real v) {
        uint64_t oldBits = bits, newBits;
        do {
            newBits = floatToBits(bitsToFloat(oldBits) + v);
        } while (!bits.compare_exchange_weak(oldBits, newBits));
    }

    void store(Real v) {
        bits.store(floatToBits(v), std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> bits;
};

struct AtomicVec {
    AtomicFloat x, y, z;

    void splat(const Vector3 c) {
        x.add(c.x);
        y.add(c.y);
        z.add(c.z);
    }
};

int main(int argc, char *argv[]) {
    int w = 1024, h = 768, samps = argc == 2 ? atoi(argv[1]) : 4; // # samples
    uint32_t nChains = 2048;
    uint32_t nMutations = std::ceil(Real(w) * h * samps / nChains);

    std::vector<uint32_t> seeds;
    for (int i = 0; i < nBootstrap; i++) {
        seeds.emplace_back(rand());
    }
    std::vector<Real> weights;
    for (int i = 0; i < nBootstrap; i++) {
        Sampler sampler(w, h, seeds[i]);
        weights.emplace_back(ScalarContributionFunction(radiance(sampler, true).Li));
    }
    std::vector<Real> cdf;
    cdf.emplace_back(0);
    for (auto &i: weights) {
        cdf.emplace_back(cdf.back() + i);
    }
    b = cdf.back() / nBootstrap;
    printf("nChains = %d, nMutations = %d\nb = %lf\n", nChains, nMutations, b);

    std::vector<AtomicVec> c(w * h);
    std::atomic<uint64_t> totalMutations(0);
    unsigned int mainSeed = rand();
    auto write = [&](const RadianceRecord &record) {
        auto &r = record.Li;
        int i = (h - record.y - 1) * w + record.x;
        c[i].splat(Vector3(removeNaN(r.x), removeNaN(r.y), removeNaN(r.z)));
    };
    std::mutex mutex;
    int32_t count = 0;
#pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < nChains; i++) {
        Real r = random(&mainSeed) * cdf.back();
        int k = 1;
        for (; k <= nBootstrap; k++) {
            if (cdf[k - 1] < r && r <= cdf[k]) {
                break;
            }
        }
        k -= 1;
        Sampler sampler(w, h, seeds[k]);
        sampler.current = radiance(sampler, true); // retrace path
        sampler.seed = rand(); // reseeding
        for (int m = 0; m < nMutations; m++) {
            RadianceRecord r1, r2;
            RunMarkovChain(sampler, r1, r2);
            write(r1);
            write(r2);
            totalMutations++;
        }
        {
            std::lock_guard<std::mutex> lockGuard(mutex);
            count++;
            printf("Done markov chain %d/%d, acceptance rate %lf\n", count, nChains,
                   Real(sampler.a) / Real(sampler.a + sampler.r));
        }
    }
    for (auto &i:c) {
        i.x = i.x * (1.0 / Real(samps));
        i.y = i.y * (1.0 / Real(samps));
        i.z = i.z * (1.0 / Real(samps));
    }
    FILE *f = fopen("image.ppm", "w");         // Write image to PPM file.
    fprintf(f, "P3\n%d %d\n%d\n", w, h, 255);
    for (int i = 0; i < w * h; i++)
        fprintf(f, "%d %d %d ", toInt(c[i].x), toInt(c[i].y), toInt(c[i].z));
}
