#ifndef SMALLPSSMLT_H
#define SMALLPSSMLT_H

#define Real float
#define Vector3 float3

struct Ray {
    Vector3 o, d;
};

enum Refl_t {
    DIFF, SPEC, REFR
};  // material types, used in radiance()
struct Sphere {
    Real rad;       // radius
    Vector3 p, e, c;      // position, emission, color
    Refl_t refl;      // reflection type (DIFFuse, SPECular, REFRactive)

    Real intersect(const Ray &r) { // returns distance, 0 if nohit
        Vector3 op = p - r.o; // Solve t^2*d.d + 2*t*(o-p).d + (o-p).(o-p)-R^2 = 0
        Real t, eps = 1e-4, b = dot(op, r.d), det = b * b - dot(op, op) + rad * rad;
        if (det < 0)
			return 0;
		else
			det = sqrt(det);
        return (t = b - det) > eps ? t : ((t = b + det) > eps ? t : 0);
    }
};

struct PrimarySample {
    Real value;
    Real _backup;
    uint64_t lastModificationIteration;
    uint64_t lastModifiedBackup;

    void backup() {
        _backup = value;
        lastModifiedBackup = lastModificationIteration;
    }

    void restore() {
        value = _backup;
        lastModificationIteration = lastModifiedBackup;
    }
};

#endif