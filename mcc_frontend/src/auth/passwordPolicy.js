export const MIN_PASSWORD = 12;

/**
 * Mirrors the backend's PasswordPolicy for instant feedback; the server has
 * the final say. Returns the problem, or null when the password is acceptable.
 */
export function passwordProblem(password, username) {
  if ([...password].length < MIN_PASSWORD) return `At least ${MIN_PASSWORD} characters`;
  if (new TextEncoder().encode(password).length > 72) return "At most 72 bytes";
  if (username && password.toLowerCase().includes(username.toLowerCase())) return "Must not contain the username";
  return null;
}
