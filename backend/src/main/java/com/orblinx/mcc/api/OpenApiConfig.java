package com.orblinx.mcc.api;

import io.swagger.v3.oas.models.OpenAPI;
import io.swagger.v3.oas.models.info.Info;
import io.swagger.v3.oas.models.tags.Tag;

import java.util.List;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

/** The title and grouping shown at the top of Swagger UI. */
@Configuration
public class OpenApiConfig {

    @Bean
    public OpenAPI mccOpenApi() {
        return new OpenAPI()
                .info(new Info()
                        .title("OrbLinx MCC API")
                        .version("1.0.0")
                        .description("""
                                REST API of the OrbLinx MCC backend. It receives the XMCC v1 subframe \
                                feed from the XDLINX core team's logic on TCP 9100, decodes every \
                                downlinked parameter to an engineering value, stores it in SQLite and \
                                serves it here. Live updates are pushed on the WebSocket /ws/telemetry, \
                                which OpenAPI cannot describe; see the Docs page, section Live stream."""))
                .tags(List.of(
                        new Tag().name("Telemetry").description("Latest values, history and feed status"),
                        new Tag().name("Decode").description("Buffered subframes and the byte-level decode trace"),
                        new Tag().name("Feed control").description("Start and stop the XDLINX telemetry stack"),
                        new Tag().name("Documentation").description("The live database schema")));
    }
}
