package vendor.anc.fingerprint.extension;

/* Recovered from Bp::excuteCommand reply path (AParcel_readInt32Array into
 * vec<AncStatus>): parcelable holding a single int status word.
 * Extension side sets status = (ret != 0); see ExtensionCommandWork.
 */
@VintfStability
parcelable AncStatus {
    int status;
}
