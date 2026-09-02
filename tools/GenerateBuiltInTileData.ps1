$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Drawing

$script:Columns = 80
$script:Rows = 45
$script:TileSize = 16
$script:DataRoot = Join-Path $PSScriptRoot '..\NoviceResources\data'
$script:LevelRoot = Join-Path $script:DataRoot 'levels'
$script:TileRoot = Join-Path $script:DataRoot 'tiles'

New-Item -ItemType Directory -Force $script:LevelRoot | Out-Null
New-Item -ItemType Directory -Force $script:TileRoot | Out-Null

function New-TileLayer {
    param([int]$Fill = 0)

    $layer = [System.Collections.Generic.List[object]]::new()
    for ($row = 0; $row -lt $script:Rows; ++$row) {
        $cells = [System.Collections.Generic.List[int]]::new()
        for ($column = 0; $column -lt $script:Columns; ++$column) {
            $cells.Add($Fill)
        }
        $layer.Add($cells.ToArray())
    }
    return $layer.ToArray()
}

function Set-TileRectangle {
    param(
        [object[]]$Layer,
        [int]$Left,
        [int]$Top,
        [int]$Width,
        [int]$Height,
        [int]$TileId
    )

    for ($row = $Top; $row -lt $Top + $Height; ++$row) {
        for ($column = $Left; $column -lt $Left + $Width; ++$column) {
            $Layer[$row][$column] = $TileId
        }
    }
}

function Write-JsonFile {
    param([string]$Path, [object]$Value)

    $json = $Value | ConvertTo-Json -Depth 32
    [System.IO.File]::WriteAllText(
        [System.IO.Path]::GetFullPath($Path),
        $json + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false))
}

function New-Rules {
    param([double]$TotalLength, [bool]$ShowHints)

    return [ordered]@{
        total_length = $TotalLength
        minimum_slack_ratio = 1.05
        show_target_connections = $ShowHints
        vessel = [ordered]@{
            color = '#861B2B'
            base_width = 16.0
            tip_width = 16.0
            width_variation = 0.16
        }
    }
}

function New-Connection {
    param(
        [string]$Id,
        [string]$From,
        [string]$To,
        [int]$Points = 10,
        [double]$Thickness = 1.0,
        [double]$Delay = 0.04,
        [double]$Direction = 0.0
    )

    return [ordered]@{
        id = $Id
        from = $From
        to = $To
        point_count = $Points
        thickness_scale = $Thickness
        follow_delay_seconds = $Delay
        initial_direction_degrees = $Direction
    }
}

function New-Node {
    param(
        [string]$Id,
        [string]$Type,
        [int]$Column,
        [int]$Row,
        [bool]$Root,
        [bool]$Goal,
        [int]$Incoming,
        [int]$Outgoing
    )

    return [ordered]@{
        id = $Id
        type_id = $Type
        column = $Column
        row = $Row
        is_root = $Root
        is_goal = $Goal
        max_incoming = $Incoming
        max_outgoing = $Outgoing
    }
}

$catalog = [ordered]@{
    schema_version = 1
    canvas = [ordered]@{ width = 1280; height = 720; tile_size = 16 }
    tileset = 'tileset.json'
    node_types = 'node_types.json'
    levels = @('levels/first_link.json', 'levels/around_block.json', 'levels/clot_path.json')
}
Write-JsonFile (Join-Path $script:DataRoot 'catalog.json') $catalog

$tileset = [ordered]@{
    schema_version = 1
    atlas_path = 'tiles/organ_atlas.png'
    atlas_columns = 4
    atlas_rows = 2
    tiles = @(
        [ordered]@{ id = 1; name = 'tissue'; atlas_column = 0; atlas_row = 0 },
        [ordered]@{ id = 2; name = 'bone'; atlas_column = 1; atlas_row = 0 },
        [ordered]@{ id = 3; name = 'heart'; atlas_column = 2; atlas_row = 0 },
        [ordered]@{ id = 4; name = 'lung'; atlas_column = 3; atlas_row = 0 },
        [ordered]@{ id = 5; name = 'kidney'; atlas_column = 0; atlas_row = 1 },
        [ordered]@{ id = 6; name = 'liver'; atlas_column = 1; atlas_row = 1 },
        [ordered]@{ id = 7; name = 'brain'; atlas_column = 2; atlas_row = 1 },
        [ordered]@{ id = 8; name = 'stomach'; atlas_column = 3; atlas_row = 1 }
    )
}
Write-JsonFile (Join-Path $script:DataRoot 'tileset.json') $tileset

$nodeTypes = [ordered]@{
    schema_version = 1
    node_types = @(
        [ordered]@{
            type_id = 'heart'; display_name = 'HEART'
            stamp = @(
                @(0, 3, 0, 3, 0),
                @(3, 3, 3, 3, 3),
                @(3, 3, 3, 3, 3),
                @(0, 3, 3, 3, 0),
                @(0, 0, 3, 0, 0))
            anchor = [ordered]@{ column = 2; row = 2 }
        },
        [ordered]@{
            type_id = 'lung'; display_name = 'LUNG'
            stamp = @(
                @(0, 4, 4, 0, 4, 4, 0),
                @(4, 4, 4, 0, 4, 4, 4),
                @(4, 4, 4, 4, 4, 4, 4),
                @(0, 4, 4, 4, 4, 4, 0),
                @(0, 0, 4, 0, 4, 0, 0))
            anchor = [ordered]@{ column = 3; row = 2 }
        },
        [ordered]@{
            type_id = 'kidney'; display_name = 'KIDNEY'
            stamp = @(
                @(0, 5, 5, 0, 0),
                @(5, 5, 5, 5, 0),
                @(5, 5, 0, 5, 5),
                @(0, 5, 5, 5, 5),
                @(0, 0, 5, 5, 0))
            anchor = [ordered]@{ column = 2; row = 3 }
        },
        [ordered]@{
            type_id = 'liver'; display_name = 'LIVER'
            stamp = @(
                @(0, 6, 6, 6, 6, 0),
                @(6, 6, 6, 6, 6, 6),
                @(6, 6, 6, 6, 6, 0),
                @(0, 6, 6, 6, 0, 0))
            anchor = [ordered]@{ column = 3; row = 2 }
        },
        [ordered]@{
            type_id = 'brain'; display_name = 'BRAIN'
            stamp = @(
                @(0, 7, 7, 7, 7, 7, 0),
                @(7, 7, 7, 7, 7, 7, 7),
                @(7, 7, 7, 7, 7, 7, 7),
                @(0, 7, 7, 7, 7, 7, 0),
                @(0, 0, 7, 7, 7, 0, 0))
            anchor = [ordered]@{ column = 3; row = 2 }
        },
        [ordered]@{
            type_id = 'stomach'; display_name = 'STOMACH'
            stamp = @(
                @(0, 0, 8, 8, 0),
                @(0, 8, 8, 8, 0),
                @(0, 8, 8, 8, 8),
                @(8, 8, 8, 8, 0),
                @(8, 8, 8, 0, 0),
                @(0, 8, 0, 0, 0))
            anchor = [ordered]@{ column = 2; row = 3 }
        }
    )
}
Write-JsonFile (Join-Path $script:DataRoot 'node_types.json') $nodeTypes

$firstBackground = New-TileLayer 1
$firstObstacles = New-TileLayer
$first = [ordered]@{
    schema_version = 1
    id = 'first_link'
    title = 'FIRST LINK'
    background_color = '#170B12'
    rules = New-Rules 700.0 $true
    layers = [ordered]@{ background = $firstBackground; obstacles = $firstObstacles }
    nodes = @(
        (New-Node 'heart_a' 'heart' 18 20 $true $false 0 1),
        (New-Node 'brain_a' 'brain' 55 20 $false $true 1 0))
    connections = @(
        (New-Connection 'heart_a_to_brain_a' 'heart_a' 'brain_a' 10 1.0 0.03 0.0))
}
Write-JsonFile (Join-Path $script:LevelRoot 'first_link.json') $first

$aroundBackground = New-TileLayer 1
$aroundObstacles = New-TileLayer
Set-TileRectangle $aroundObstacles 34 16 12 14 2
$around = [ordered]@{
    schema_version = 1
    id = 'around_block'
    title = 'AROUND THE RIB'
    background_color = '#160A10'
    rules = New-Rules 1450.0 $true
    layers = [ordered]@{ background = $aroundBackground; obstacles = $aroundObstacles }
    nodes = @(
        (New-Node 'heart_a' 'heart' 10 20 $true $false 0 1),
        (New-Node 'lung_a' 'lung' 28 5 $false $false 1 1),
        (New-Node 'kidney_a' 'kidney' 29 34 $false $false 1 1),
        (New-Node 'brain_a' 'brain' 65 20 $false $true 2 0))
    connections = @(
        (New-Connection 'heart_a_to_lung_a' 'heart_a' 'lung_a' 9 1.0 0.02 -35.0),
        (New-Connection 'heart_a_to_kidney_a' 'heart_a' 'kidney_a' 11 0.95 0.06 35.0),
        (New-Connection 'lung_a_to_brain_a' 'lung_a' 'brain_a' 10 1.0 0.04 22.0),
        (New-Connection 'kidney_a_to_brain_a' 'kidney_a' 'brain_a' 12 1.05 0.08 -22.0))
}
Write-JsonFile (Join-Path $script:LevelRoot 'around_block.json') $around

$clotBackground = New-TileLayer 1
$clotObstacles = New-TileLayer
Set-TileRectangle $clotObstacles 35 18 3 9 2
Set-TileRectangle $clotObstacles 41 20 3 5 2
$clot = [ordered]@{
    schema_version = 1
    id = 'clot_path'
    title = 'DOUBLE CIRCULATION'
    background_color = '#14090F'
    rules = New-Rules 3100.0 $false
    layers = [ordered]@{ background = $clotBackground; obstacles = $clotObstacles }
    nodes = @(
        (New-Node 'heart_a' 'heart' 6 9 $true $false 0 2),
        (New-Node 'heart_b' 'heart' 6 31 $true $false 0 2),
        (New-Node 'lung_a' 'lung' 23 5 $false $false 2 1),
        (New-Node 'lung_b' 'lung' 23 35 $false $false 2 1),
        (New-Node 'liver_a' 'liver' 40 10 $false $false 1 1),
        (New-Node 'kidney_a' 'kidney' 40 30 $false $false 1 1),
        (New-Node 'lung_c' 'lung' 54 19 $false $false 2 2),
        (New-Node 'brain_a' 'brain' 69 6 $false $true 1 0),
        (New-Node 'brain_b' 'brain' 69 32 $false $true 1 0))
    connections = @(
        (New-Connection 'heart_a_to_lung_a' 'heart_a' 'lung_a' 8 0.90 0.01 -20.0),
        (New-Connection 'heart_a_to_lung_b' 'heart_a' 'lung_b' 12 1.05 0.09 35.0),
        (New-Connection 'heart_b_to_lung_a' 'heart_b' 'lung_a' 12 0.95 0.08 -35.0),
        (New-Connection 'heart_b_to_lung_b' 'heart_b' 'lung_b' 9 1.10 0.02 20.0),
        (New-Connection 'lung_a_to_liver_a' 'lung_a' 'liver_a' 10 1.00 0.04 12.0),
        (New-Connection 'lung_b_to_kidney_a' 'lung_b' 'kidney_a' 11 0.95 0.06 -12.0),
        (New-Connection 'liver_a_to_lung_c' 'liver_a' 'lung_c' 9 1.05 0.03 18.0),
        (New-Connection 'kidney_a_to_lung_c' 'kidney_a' 'lung_c' 12 1.00 0.08 -18.0),
        (New-Connection 'lung_c_to_brain_a' 'lung_c' 'brain_a' 10 0.90 0.03 -28.0),
        (New-Connection 'lung_c_to_brain_b' 'lung_c' 'brain_b' 11 1.10 0.07 28.0))
}
Write-JsonFile (Join-Path $script:LevelRoot 'clot_path.json') $clot

$atlasPath = Join-Path $script:TileRoot 'organ_atlas.png'
$bitmap = [System.Drawing.Bitmap]::new(64, 32, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
    $palette = @(
        @('#3A1728', '#4A1C30', '#2B101E'),
        @('#D4C7A8', '#EFE5C8', '#9B9075'),
        @('#9B1830', '#C52B3E', '#611020'),
        @('#A84454', '#D06773', '#722A38'),
        @('#70405F', '#9B5B83', '#4D2940'),
        @('#7B3E2B', '#A85A3A', '#51271F'),
        @('#C06B80', '#E18EA1', '#874456'),
        @('#B65D38', '#DC8150', '#743720'))
    for ($tile = 0; $tile -lt $palette.Count; ++$tile) {
        $originX = ($tile % 4) * $script:TileSize
        $originY = [Math]::Floor($tile / 4) * $script:TileSize
        $base = [System.Drawing.ColorTranslator]::FromHtml($palette[$tile][0])
        $light = [System.Drawing.ColorTranslator]::FromHtml($palette[$tile][1])
        $dark = [System.Drawing.ColorTranslator]::FromHtml($palette[$tile][2])
        for ($y = 0; $y -lt $script:TileSize; ++$y) {
            for ($x = 0; $x -lt $script:TileSize; ++$x) {
                $color = $base
                if ((($x + $y + $tile) % 11) -eq 0) { $color = $light }
                if ((($x * 3 + $y * 5 + $tile) % 17) -eq 0) { $color = $dark }
                if ($x -eq 0 -or $y -eq 0) { $color = $dark }
                $bitmap.SetPixel($originX + $x, $originY + $y, $color)
            }
        }
    }
    $bitmap.Save($atlasPath, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $bitmap.Dispose()
}

Write-Host 'Generated built-in JSON tile data and placeholder atlas.'
