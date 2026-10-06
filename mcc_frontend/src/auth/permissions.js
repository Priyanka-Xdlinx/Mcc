/**
 * Whether the signed-in user holds a permission. The list comes with the user
 * from the backend (see backend auth/Permission.java), so a screen asks
 * `can(user, "DISPLAY_EDIT")` before it shows a control. The server still
 * refuses anything the screen lets through; this only keeps the screen honest.
 */
export const can = (user, permission) => user?.permissions?.includes(permission) ?? false;
