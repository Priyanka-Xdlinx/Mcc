package com.orblinx.mcc;

import java.nio.file.Path;
import javax.sql.DataSource;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import org.sqlite.SQLiteConfig;
import org.sqlite.SQLiteDataSource;

/**
 * Two SQLite files: mcc.db (decode, telemetry, display; store.DecodeSchemaLoader
 * fills the decode tables from the XDLINX tree on a fresh one) and mcc-auth.db
 * (accounts, sessions, signing keys; see auth.AccountRepo). Each repository
 * creates its own tables with CREATE TABLE IF NOT EXISTS, so an existing file
 * is never rebuilt and a missing one is created on first use.
 */
@SpringBootApplication
@org.springframework.scheduling.annotation.EnableScheduling
public class MccApplication {

    public static void main(String[] args) {
        SpringApplication.run(MccApplication.class, args);
    }

    /** The decode/telemetry/display database. */
    @Bean
    DataSource dataSource(@Value("${mcc.db}") String db) {
        return sqliteDataSource(db);
    }

    /**
     * WAL lets readers run while a write is in progress, and a busy timeout
     * makes a writer wait instead of failing with SQLITE_BUSY ("the database
     * file is locked") when telemetry ingest and an API write land at once.
     */
    public static SQLiteDataSource sqliteDataSource(String path) {
        Path abs = Path.of(path).toAbsolutePath().normalize();
        SQLiteConfig config = new SQLiteConfig();
        config.setJournalMode(SQLiteConfig.JournalMode.WAL);
        config.setBusyTimeout(5000);
        config.enforceForeignKeys(true);
        SQLiteDataSource ds = new SQLiteDataSource(config);
        ds.setUrl("jdbc:sqlite:" + abs);
        return ds;
    }
}
