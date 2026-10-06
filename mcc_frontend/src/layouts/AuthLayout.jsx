import logoInk from "../assets/images/logo-ink-800.png";
import logoWhite from "../assets/images/logo-white-800.png";
import { Box, Stack, Text } from "../components/common/index.js";
import useResolvedTheme from "../hooks/useResolvedTheme.js";

/** Logo, product name and a card: the frame of the sign-in and password pages. */
export default function AuthLayout({ children }) {
  const resolved = useResolvedTheme();
  return (
    <Box
      component="main"
      display="flex"
      alignItems="center"
      justifyContent="center"
      minHeight="100svh"
      p={2}
      sx={{ bgcolor: "background.default" }}
    >
      <Stack
        component="section"
        gap={2}
        sx={{
          width: "100%",
          maxWidth: 420,
          p: 4,
          bgcolor: "background.paper",
          border: 1,
          borderColor: "divider",
          borderRadius: 2,
          boxShadow: 4,
        }}
      >
        <img src={resolved === "dark" ? logoWhite : logoInk} alt="XDLINX Space Labs" height={34} style={{ alignSelf: "flex-start" }} />
        <Box>
          <Text variant="heading" component="h1">
            OrbLinx MCC
          </Text>
          <Text muted>Mission control · XMCC telemetry</Text>
        </Box>
        {children}
      </Stack>
    </Box>
  );
}
