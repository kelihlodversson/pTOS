"""Require distinct native B2 success and unsupported-return verdicts."""

DEFERRED = (
    'native retained IRQ capture-only and uncredited root return: PASS',
    'native interrupted USER A IRQ / B USER execution / saved A restart: PASS',
    'native retained IRQ with managed exact-child wait: PASS',
    'native retained IRQ armed/acknowledged exit and suspended group cancellation: PASS',
)
REFUSED = 'native retained IRQ unsupported CPU or ABI ordinary return: PASS'


def missing_irq_evidence(text, profile):
    lines = set(text.splitlines())
    required, forbidden = (DEFERRED, (REFUSED,)) if profile == 'deferred' else ((REFUSED,), DEFERRED)
    return ([f'missing {marker}' for marker in required if marker not in lines] +
            [f'unexpected {marker}' for marker in forbidden if marker in lines])
