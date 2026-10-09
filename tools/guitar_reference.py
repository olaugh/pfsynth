"""Recording clocks and release gates for reference-driven guitar fitting.

A GAPS MIDI timestamp is not proof that a downloaded video has the same clock.
Corrections are keyed by the exact decoded audio hash, never by title alone.
"""
from pathlib import Path
import hashlib,json,math,warnings


def reference_origin(audio_path,midi_origin,gaps_id,manifest=None):
    manifest=Path(manifest) if manifest else Path(__file__).with_name('guitar_reference_offsets.json')
    known=json.loads(manifest.read_text()) if manifest.exists() else {}
    digest=hashlib.sha256(Path(audio_path).read_bytes()).hexdigest()
    entry=known.get(gaps_id)
    if entry:
        if digest!=entry['audio_sha256']:
            raise ValueError(f'{gaps_id}: recording hash changed; verify audio alignment before refitting')
        offset=float(entry['audio_origin_seconds']);method=entry['method'];verified=True
    else:
        offset=float(midi_origin);method='unverified assumption: audio clock equals MIDI clock';verified=False
        warnings.warn(f'{gaps_id}: audio origin has not been verified against this recording',stacklevel=2)
    if not math.isfinite(offset) or offset<0:raise ValueError('Audio origin must be finite and nonnegative')
    return offset,dict(midi_origin_seconds=float(midi_origin),source_audio_offset_seconds=offset,
        audio_sha256=digest,verified=verified,method=method)


def fit_guard(velocities,constant_error,fitted_error,min_improvement=.05):
    """Reject invalid/collapsed or non-improving candidates before web export.

    This is a numerical gate only. Passing it still requires a blind listening vote.
    Evaluate AFTER physical caps, using the audible room and body.
    """
    v=list(velocities)
    if not v or any(not math.isfinite(x) or x<=0 for x in v):
        raise ValueError('Fit produced a silent or invalid note; preserve the previous performance')
    run=0
    for x in v:
        run=run+1 if x<5 else 0
        if run>=3:raise ValueError('Fit collapsed consecutive notes; check alignment and room')
    if not all(math.isfinite(x) for x in (constant_error,fitted_error)):
        raise ValueError('Fit comparison is not finite')
    if fitted_error>=constant_error-min_improvement:
        raise ValueError('Fitted performance does not improve on constant note strength; do not export it')
    return dict(constant_error_db=float(constant_error),fitted_error_db=float(fitted_error),
        minimum_velocity=float(min(v)),numerical_gate='passed',listening_status='unaccepted')
