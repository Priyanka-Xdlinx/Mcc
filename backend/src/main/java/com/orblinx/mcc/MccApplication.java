package com.orblinx.mcc;

import java.nio.file.Files;
import java.nio.file.Path;
import javax.sql.DataSource;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import org.sqlite.SQLiteDataSource;

@SpringBootApplication
@org.springframework.scheduling.annotation.EnableScheduling
public class MccApplication {

    public static void main(String[] args) {
        SpringApplication.run(MccApplication.class, args);
    }

    /**
     * The decode schema, built by schema/build_schema.py from the MIB JSONs.
     *
     * Failing loudly when it is absent is deliberate: an empty database would
     * start cleanly and then decode nothing, which looks like a quiet feed
     * rather than a missing schema.
     */
    @Bean
    DataSource dataSource(@Value("${mcc.db}") String db) throws Exception {
        Path path = Path.of(db).toAbsolutePath().normalize();
        if (!Files.exists(path)) {
            throw new IllegalStateException(
                "decode schema not found at " + path
                + " - build it first:  python3 schema/build_schema.py");
        }
        SQLiteDataSource ds = new SQLiteDataSource();
        ds.setUrl("jdbc:sqlite:" + path);
        ds.setEnforceForeignKeys(true);
        return ds;
    }
}
