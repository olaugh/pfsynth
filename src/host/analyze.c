/* analyze.c - measure real piano notes to fit the pfsynth model. HOST-ONLY.
 *
 * Reads a single-note WAV (e.g. a Salamander Grand sample) and measures the
 * things pf_string / pf_board parameterize, so the model can be fit to a real
 * instrument instead of tuned by ear:
 *
 *   - partial frequencies -> inharmonicity B   (f_n ~ n*f0*sqrt(1+B*n^2))
 *   - per-partial amplitude decay -> T60        (and the fundamental T60)
 *   - spectral centroid (attack + sustain)      (brightness)
 *
 * No FFT library: partials are tracked with the Goertzel algorithm at the
 * (refined) partial frequencies, which is all we need and stays dependency-free.
 *
 *   analyze <file.wav> <midi-note>
 *
 * Prints one machine-readable line:  note f0 B T60 centroid ... per-partial T60s
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- minimal WAV reader: scans chunks, returns mono double @ sr ---- */
static int read_wav(const char *path, double **out, long *n_out, int *sr_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        fclose(f); fprintf(stderr, "%s: not a WAV\n", path); return 1;
    }
    int channels = 0, sr = 0, bits = 0;
    long data_off = -1; unsigned long data_len = 0;
    for (;;) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        unsigned long sz = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((unsigned long)ch[7] << 24);
        if (!memcmp(ch, "fmt ", 4)) {
            unsigned char fmt[16];
            if (fread(fmt, 1, 16, f) != 16) break;
            channels = fmt[2] | (fmt[3] << 8);
            sr       = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
            bits     = fmt[14] | (fmt[15] << 8);
            if (sz > 16) fseek(f, sz - 16, SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            data_off = ftell(f); data_len = sz; break;
        } else {
            fseek(f, sz + (sz & 1), SEEK_CUR);
        }
    }
    if (data_off < 0 || bits != 16 || channels < 1) {
        fclose(f); fprintf(stderr, "%s: unsupported (bits=%d ch=%d)\n", path, bits, channels);
        return 1;
    }
    long frames = (long)(data_len / (2 * channels));
    double *m = malloc((size_t)frames * sizeof(double));
    fseek(f, data_off, SEEK_SET);
    for (long i = 0; i < frames; i++) {
        long acc = 0;
        for (int c = 0; c < channels; c++) {
            unsigned char b[2];
            if (fread(b, 1, 2, f) != 2) { frames = i; break; }
            acc += (short)(b[0] | (b[1] << 8));
        }
        m[i] = (double)acc / channels / 32768.0;
    }
    fclose(f);
    *out = m; *n_out = frames; *sr_out = sr;
    return 0;
}

/* Goertzel power (squared magnitude) at frequency f over samples [a, a+n) */
static double gpow(const double *x, long a, long n, double f, double sr)
{
    double w = 2.0 * M_PI * f / sr, c = 2.0 * cos(w), s1 = 0, s2 = 0, s0;
    for (long i = a; i < a + n; i++) { s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
static double gmag(const double *x, long a, long n, double f, double sr)
{
    double p = gpow(x, a, n, f, sr);
    return p > 0 ? sqrt(p) : 0.0;
}

/* refine a partial's frequency by scanning a +/- window for the magnitude peak */
static double refine(const double *x, long a, long n, double fc, double sr)
{
    double best = fc, bestm = -1;
    double lo = fc * 0.97, hi = fc * 1.03, step = fc * 0.0008;
    for (double f = lo; f <= hi; f += step) {
        double m = gpow(x, a, n, f, sr);
        if (m > bestm) { bestm = m; best = f; }
    }
    return best;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: analyze <file.wav> <midi-note>\n"); return 2; }
    int midi = atoi(argv[2]);
    double f0 = 440.0 * pow(2.0, (midi - 69) / 12.0);

    double *x; long n; int sr;
    if (read_wav(argv[1], &x, &n, &sr)) return 1;

    /* onset = first sample past 1% of peak */
    double peak = 0;
    for (long i = 0; i < n; i++) { double a = fabs(x[i]); if (a > peak) peak = a; }
    long onset = 0;
    for (long i = 0; i < n; i++) if (fabs(x[i]) > 0.01 * peak) { onset = i; break; }

    /* --- partials + inharmonicity, measured on a stable window after the attack --- */
    long wa = onset + (long)(0.08 * sr);          /* skip the strike transient */
    long ww = (long)(0.30 * sr);                  /* analysis window */
    if (wa + ww > n) ww = n - wa;

    int maxn = (int)(0.45 * sr / f0);
    if (maxn > 40) maxn = 40;
    double pf[64], pm[64]; int np = 0;
    /* Regress (f_k/k)^2 on k^2:  (f_k/k)^2 = f0^2 + (f0^2 B) k^2, so the real f0
     * (stretched tuning) and B both fall out of a line fit -> robust, no
     * dependence on the partial being at exactly the equal-tempered frequency. */
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int bc = 0;
    for (int k = 1; k <= maxn; k++) {
        double fc = k * f0;
        if (fc > 0.45 * sr) break;
        double fr = refine(x, wa, ww, fc, sr);
        double mg = gmag(x, wa, ww, fr, sr);
        pf[np] = fr; pm[np] = mg; np++;
        if (k >= 1 && mg > 0.03 * pm[0]) {        /* strong partials only */
            double yk = (fr / k) * (fr / k), xk = (double)(k * k);
            sx += xk; sy += yk; sxx += xk * xk; sxy += xk * yk; bc++;
        }
    }
    double B = 0.0, f0fit = f0;
    if (bc >= 3) {
        double det = bc * sxx - sx * sx;
        double slope = (bc * sxy - sx * sy) / det;     /* = f0^2 * B */
        double icpt  = (sy * sxx - sx * sxy) / det;    /* = f0^2 */
        if (icpt > 0) { f0fit = sqrt(icpt); B = slope / icpt; }
    }

    /* --- T60 of the fundamental: slope of the dB envelope over the singing
     * "aftersound", skipping the fast prompt at the very start and stopping
     * before the note runs out / hits the noise floor. --- */
    double ffund = f0fit;
    int nw = 0; double tdb[400], tt[400];
    double win = 0.10;                            /* 100 ms windows */
    long wlen = (long)(win * sr);
    for (long a = onset; a + wlen < n && nw < 400; a += wlen) {
        double m = gmag(x, a, wlen, ffund, sr);
        tdb[nw] = 20.0 * log10(m + 1e-12);
        tt[nw]  = (a - onset) / (double)sr;
        nw++;
    }
    int pk = 0; for (int i = 0; i < nw; i++) if (tdb[i] > tdb[pk]) pk = i;
    /* fit from ~0.4 s past the peak (past the prompt) down to peak-40 dB */
    int start = pk + (int)(0.4 / win);
    if (start >= nw) start = pk;
    double ax = 0, ay = 0, axx = 0, axy = 0; int cnt = 0;
    for (int i = start; i < nw; i++) {
        if (tdb[i] < tdb[pk] - 40.0) break;
        ax += tt[i]; ay += tdb[i]; axx += tt[i] * tt[i]; axy += tt[i] * tdb[i]; cnt++;
    }
    double slope = (cnt > 3) ? (cnt * axy - ax * ay) / (cnt * axx - ax * ax) : 0.0;
    double t60 = (slope < -0.05) ? -60.0 / slope : 999.0;

    /* --- spectral centroid: attack (first 50 ms) and sustain window --- */
    double cn = 0, cd = 0;
    for (double f = f0 * 0.5; f < 0.45 * sr; f *= 1.03) {
        double m = gpow(x, onset, (long)(0.05 * sr), f, sr);
        cn += f * m; cd += m;
    }
    double cen_attack = cd > 0 ? cn / cd : 0;
    cn = cd = 0;
    for (double f = f0 * 0.5; f < 0.45 * sr; f *= 1.03) {
        double m = gpow(x, wa, ww, f, sr);
        cn += f * m; cd += m;
    }
    double cen_sustain = cd > 0 ? cn / cd : 0;

    /* machine-readable: note f0 B t60 cen_attack cen_sustain npartials */
    printf("%d %.2f %.6e %.2f %.0f %.0f %d\n",
           midi, f0, B, t60, cen_attack, cen_sustain, np);
    free(x);
    return 0;
}
