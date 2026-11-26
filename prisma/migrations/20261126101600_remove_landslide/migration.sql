DELETE FROM "ZoneReading" WHERE "zone" = 'landslide';
DELETE FROM "ZoneStatus" WHERE "zone" = 'landslide';
DELETE FROM "HazardEvent" WHERE "zone" = 'landslide';
ALTER TABLE "HazardEvent" DROP COLUMN "landslide";
