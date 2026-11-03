package vendor.anc.fingerprint.extension;

/* Recovered from BpFingerprintExtensionClientCallback::onExcuteCommand disasm:
 * parcel order: int64, int32, int32, byte[].
 * (stock spelling "onExcuteCommand" kept for wire compat)
 */
@VintfStability
interface IFingerprintExtensionClientCallback {
    void onExcuteCommand(long l, int a, int b, in byte[] data);
}
