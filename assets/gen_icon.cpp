#include <cmath>
#include <vector>
#include <string>
#include <cstdio>
using namespace std;

typedef struct {
    double x1, y1;
    double x2, y2;
} Stroke;

static double PointSegmentDistance(double px, double py, double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1, dy = y2 - y1;
    double len_sq = dx * dx + dy * dy;
    double t = len_sq > 0.0 ? ((px - x1) * dx + (py - y1) * dy) / len_sq : 0.0;

    t = max(0.0, min(1.0, t));
    double cx = x1 + t * dx, cy = y1 + t * dy;
    double ex = px - cx, ey = py - cy;
    return sqrt(ex * ex + ey * ey);
}

int main()
{
    // larger than the 32x32 runtime window icon: file
    // manager grid views often show icons at this size
    const int Size = 64;
    const double Corner_Radius = 12.0;
    const double Stroke_Thickness = 4.4;
    const Stroke Glyph[] = {
        {18, 18, 34, 32}, // '>' [upper diagonal]
        {34, 32, 18, 46}, // '>' [lower diagonal]
        {38, 46, 54, 46}, // '_'
    };

    const char *Bg = "#181825";
    const char *Fg = "#89DCEB";

    vector<string> Rows;
    Rows.reserve(Size);
    for (int y = 0; y < Size; y++)
    {
        string row;
        row.reserve(Size);
        for (int x = 0; x < Size; x++)
        {
            double corner_cx = -1, corner_cy = -1;
            if (x < Corner_Radius && y < Corner_Radius)
            {
                corner_cx = Corner_Radius;
                corner_cy = Corner_Radius;
            }
            else if (x >= Size - Corner_Radius && y < Corner_Radius)
            {
                corner_cx = Size - Corner_Radius - 1;
                corner_cy = Corner_Radius;
            }
            else if (x < Corner_Radius && y >= Size - Corner_Radius)
            {
                corner_cx = Corner_Radius;
                corner_cy = Size - Corner_Radius - 1;
            }
            else if (x >= Size - Corner_Radius && y >= Size - Corner_Radius)
            {
                corner_cx = Size - Corner_Radius - 1;
                corner_cy = Size - Corner_Radius - 1;
            }

            bool Outside_Rounded_Corner = corner_cx >= 0 && sqrt((x - corner_cx) * (x - corner_cx) + (y - corner_cy) * (y - corner_cy)) > Corner_Radius;
            if (Outside_Rounded_Corner) {
                row += ' ';
                continue;
            }

            bool On_Glyph = false;
            for (auto &s : Glyph) {
                if (PointSegmentDistance(x + 0.5, y + 0.5, s.x1, s.y1, s.x2, s.y2) <= Stroke_Thickness) {
                    On_Glyph = true;
                    break;
                }
            }

            row += On_Glyph ? '#' : '.';
        }

        Rows.push_back(row);
    }

    printf("/* XPM */\n");
    printf("static char *icon_xpm[] = {\n");
    printf("\"%d %d 3 1\",\n", Size, Size);
    printf("\"  c None\",\n");
    printf("\". c %s\",\n", Bg);
    printf("\"# c %s\",\n", Fg);
    for (size_t i = 0; i < Rows.size(); i++)
        printf("\"%s\"%s\n", Rows[i].c_str(), i + 1 < Rows.size() ? "," : "");
    printf("};\n");

    return 0;
}
