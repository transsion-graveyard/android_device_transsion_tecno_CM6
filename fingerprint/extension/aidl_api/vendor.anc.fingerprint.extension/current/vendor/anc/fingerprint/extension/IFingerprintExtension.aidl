package vendor.anc.fingerprint.extension;

import vendor.anc.fingerprint.extension.AncStatus;
import vendor.anc.fingerprint.extension.IFingerprintExtensionClientCallback;

/* Recovered from odm/lib64/vendor.anc.fingerprint.extension-V1-ndk.so:
 * - descriptor "vendor.anc.fingerprint.extension.IFingerprintExtension"
 * - version 1, hash "a87b3c9b657bb24a7df897e225a6963e08b78063"
 * - excuteCommand(int, in byte[], out AncStatus[], out byte[])
 *   (note stock spelling "excute", kept for wire compat)
 * - setNotify(in IFingerprintExtensionClientCallback)
 * Parcel order from Bp::excuteCommand disasm: int32 cmd, byte[] in.
 */
@VintfStability
interface IFingerprintExtension {
    void excuteCommand(int cmd, in byte[] inData, out AncStatus[] status, out byte[] outData);
    void setNotify(in IFingerprintExtensionClientCallback cb);
}
