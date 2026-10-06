/** Every read of auth state a component performs. */
export const selectUser = (state) => state.auth.user;
export const selectIsAdmin = (state) => state.auth.user?.role === "ADMIN";
export const selectAuthError = (state) => state.auth.error;
export const selectSignedOutByUser = (state) => state.auth.signedOutByUser;
export const selectBootUnreachable = (state) => state.auth.bootUnreachable;

export const selectIsRestoringSession = (state) => state.auth.bootStatus === "pending";
export const selectIsSigningIn = (state) => state.auth.signInStatus === "pending";
export const selectIsSigningOut = (state) => state.auth.signOutStatus === "pending";
