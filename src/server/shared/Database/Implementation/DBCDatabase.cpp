/*
 * Copyright (C) 2009-2014 Infinitycore <http://www.infinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "DBCDatabase.h"

void DBCDatabaseConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(MAX_DBCDATABASE_STATEMENTS);

    PrepareStatement(DBC_SELECT_ITEM, "SELECT ID, DisplayId, InventoryType, Sheath FROM item ORDER BY ID", CONNECTION_SYNCH);

}
