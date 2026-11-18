/*
 Justification des methodes :

 - Fond simple (receipt.jpg, cell_pic.jpg) : la page est nettement plus claire que le fond.
   Le seuillage d'Otsu sur la luminosite separe les deux sans parametre a regler.

 - Fond texture et clair (desk.JPG) : le bois clair a une luminosite proche de celle du papier,
   donc Otsu seul deborde sur le fond. On ajoute un seuillage d'Otsu sur la saturation (HSV) :
   le papier est blanc (peu sature) alors que le bois est colore. Les deux masques sont combines (ET logique).

 - Choix automatique : on teste la methode simple, et si son masque touche trop les bords de l'image
   (plus de 10 %), on bascule sur la methode texturee.
*/

#include <opencv2/opencv.hpp>
#include <iostream>
#include <algorithm>
using namespace cv;
using namespace std;

// Etat partage entre l'affichage et la souris
struct State {
    Mat base;              // image affichee (redimensionnee)
    vector<Point2f> pts;   // 4 coins (coordonnees de l'image affichee)
    int drag = -1;         // indice du point en cours de deplacement
};

// Declaration anticipee (utilisee par isTextured)
static vector<Point2f> detectCorners(const Mat& img, bool textured, Mat& maskOut);

// ---------------------------------------------------------------
// Ordonne les points : haut-gauche, haut-droite, bas-droite, bas-gauche
// ---------------------------------------------------------------
static vector<Point2f> orderPoints(const vector<Point2f>& p) {
    vector<Point2f> r(4);
    auto s = [](const Point2f& a) { return a.x + a.y; };
    auto d = [](const Point2f& a) { return a.y - a.x; };
    r[0] = *min_element(p.begin(), p.end(), [&](const Point2f& a, const Point2f& b) { return s(a) < s(b); }); // TL
    r[2] = *max_element(p.begin(), p.end(), [&](const Point2f& a, const Point2f& b) { return s(a) < s(b); }); // BR
    r[1] = *min_element(p.begin(), p.end(), [&](const Point2f& a, const Point2f& b) { return d(a) < d(b); }); // TR
    r[3] = *max_element(p.begin(), p.end(), [&](const Point2f& a, const Point2f& b) { return d(a) < d(b); }); // BL
    return r;
}

// ---------------------------------------------------------------
// Choix automatique de la methode :
// on essaie la methode simple (Otsu) ; si son masque deborde sur les bords
// de l'image, c'est que le fond ressemble a la page (fond texture/clair)
// ---------------------------------------------------------------
static bool isTextured(const Mat& img) {
    Mat m;
    detectCorners(img, false, m);                 // essai avec la methode simple
    int n = countNonZero(m.row(0)) + countNonZero(m.row(m.rows - 1))
          + countNonZero(m.col(0)) + countNonZero(m.col(m.cols - 1));
    double frac = n / (2.0 * (m.cols + m.rows));  // part du cadre de l'image couverte par le masque
    cout << "Debordement du masque sur les bords : " << frac << endl;
    return frac > 0.10;                           // seuil a verifier avec les 3 images
}

// ---------------------------------------------------------------
// Detection automatique des 4 coins
//  - fond simple   : seuillage d'Otsu sur la luminosite
//  - fond texture  : Otsu luminosite ET Otsu sur la saturation (papier peu sature)
// ---------------------------------------------------------------
static vector<Point2f> detectCorners(const Mat& img, bool textured, Mat& maskOut) {
    Mat gray, mask;
    cvtColor(img, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, gray, Size(7, 7), 0);
    threshold(gray, mask, 0, 255, THRESH_BINARY | THRESH_OTSU);       // page claire

    if (textured) {                                                    // + page peu saturee
        Mat hsv, ch[3], sat, satMask;
        cvtColor(img, hsv, COLOR_BGR2HSV);
        split(hsv, ch);
        GaussianBlur(ch[1], sat, Size(7, 7), 0);
        threshold(sat, satMask, 0, 255, THRESH_BINARY_INV | THRESH_OTSU);
        bitwise_and(mask, satMask, mask);
    }

    // Morphologie : supprime le bruit, puis comble le texte de la page
    int k = max(5, img.cols / 60);
    morphologyEx(mask, mask, MORPH_OPEN,  getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
    morphologyEx(mask, mask, MORPH_CLOSE, getStructuringElement(MORPH_ELLIPSE, Size(k, k)));
    maskOut = mask.clone();

    // Plus grand contour exterieur = la page
    vector<vector<Point>> contours;
    findContours(mask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

    vector<Point2f> res;
    if (contours.empty()) {   // rien trouve : coins de l'image
        res = { {0,0}, {(float)img.cols-1,0}, {(float)img.cols-1,(float)img.rows-1}, {0,(float)img.rows-1} };
        return res;
    }
    vector<Point> biggest = *max_element(contours.begin(), contours.end(),
        [](const vector<Point>& a, const vector<Point>& b) { return contourArea(a) < contourArea(b); });
    vector<Point> hull;
    convexHull(biggest, hull);

    // Approximation polygonale : on augmente la tolerance jusqu'a obtenir 4 sommets
    vector<Point> approx;
    double per = arcLength(hull, true);
    for (double e = 0.01; e <= 0.10; e += 0.005) {
        approxPolyDP(hull, approx, e * per, true);
        if (approx.size() <= 4) break;
    }
    if (approx.size() == 4) {
        for (size_t i = 0; i < approx.size(); i++) res.push_back(Point2f(approx[i]));
    } else {                                             // secours : rectangle englobant oriente
        Point2f r[4];
        minAreaRect(hull).points(r);
        res.assign(r, r + 4);
    }
    return orderPoints(res);
}

// ---------------------------------------------------------------
// Affichage de la detection (contour vert + points rouges numerotes)
// ---------------------------------------------------------------
static void draw(const State& s) {
    Mat v = s.base.clone();
    vector<Point> poly;
    for (size_t i = 0; i < s.pts.size(); i++) poly.push_back(Point(s.pts[i]));
    polylines(v, poly, true, Scalar(0, 255, 0), 2);
    for (int i = 0; i < 4; i++) {
        circle(v, poly[i], 8, Scalar(0, 0, 255), FILLED);
        putText(v, to_string(i + 1), poly[i] + Point(10, -10), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(255, 0, 0), 2);
    }
    putText(v, "Glisser les points | Entree: valider | m: methode | r: reset | Esc: quitter",
            Point(10, 25), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 255, 255), 1);
    imshow("Detection / ajustement", v);
}

// ---------------------------------------------------------------
// Ajustement interactif : on attrape le point le plus proche et on le deplace
// ---------------------------------------------------------------
static void onMouse(int ev, int x, int y, int, void* ud) {
    State* s = (State*)ud;
    if (ev == EVENT_LBUTTONDOWN) {
        double best = 30; s->drag = -1;
        for (int i = 0; i < 4; i++) {
            double d = norm(s->pts[i] - Point2f((float)x, (float)y));
            if (d < best) { best = d; s->drag = i; }
        }
    } else if (ev == EVENT_MOUSEMOVE && s->drag >= 0) {
        int cx = max(0, min(x, s->base.cols - 1));    // le point reste dans l'image
        int cy = max(0, min(y, s->base.rows - 1));
        s->pts[s->drag] = Point2f((float)cx, (float)cy);
        draw(*s);
    } else if (ev == EVENT_LBUTTONUP) {
        s->drag = -1;
    }
}

// ---------------------------------------------------------------
// Traite une image : detection, ajustement interactif, transformation
// Retourne -1 si l'utilisateur quitte (Echap), 0 sinon
// ---------------------------------------------------------------
int process(const string& path) {
    Mat orig = imread(path);
    if (orig.empty()) { cerr << "Image introuvable : " << path << endl; return 0; }

    // Travail sur une version reduite (rapide), puis remise a l'echelle pour la transformation finale
    double scale = min(1.0, min(650.0 / orig.rows, 900.0 / orig.cols));
    State st;
    resize(orig, st.base, Size(), scale, scale, INTER_AREA);

    // 1) Detection automatique
    bool textured = isTextured(st.base);
    Mat mask;
    st.pts = detectCorners(st.base, textured, mask);
    cout << "Methode : " << (textured ? "fond texture (luminosite + saturation)" : "fond simple (Otsu)") << endl;

    // 2) Affichage de la detection + 3) ajustement interactif
    namedWindow("Detection / ajustement", WINDOW_AUTOSIZE);
    setMouseCallback("Detection / ajustement", onMouse, &st);
    namedWindow("Masque", WINDOW_AUTOSIZE);
    moveWindow("Detection / ajustement", 0, 0);
    moveWindow("Masque", st.base.cols + 20, 0);
    imshow("Masque", mask);
    draw(st);

    while (true) {
        int key = waitKey(20);
        if (key == 27) return -1;                                 // Esc : on quitte tout
        if (key == 13 || key == 32) break;                        // Entree / Espace : valider
        if (key == 'r' || key == 'm') {                           // reset ou changement de methode
            if (key == 'm') textured = !textured;
            st.pts = detectCorners(st.base, textured, mask);
            cout << "Methode : " << (textured ? "texture" : "simple") << endl;
            imshow("Masque", mask);
            draw(st);
        }
    }

    // 4) Transformation de perspective sur l'image d'origine
    vector<Point2f> src;
    for (size_t i = 0; i < st.pts.size(); i++) src.push_back(st.pts[i] * (1.0 / scale));
    src = orderPoints(src);

    float w = (float)max(norm(src[0] - src[1]), norm(src[3] - src[2]));
    float h = (float)max(norm(src[0] - src[3]), norm(src[1] - src[2]));
    vector<Point2f> dst = { {0, 0}, {w - 1, 0}, {w - 1, h - 1}, {0, h - 1} };

    Mat M = getPerspectiveTransform(src, dst), out;
    warpPerspective(orig, out, M, Size(cvRound(w), cvRound(h)), INTER_CUBIC);

    // Un fichier resultat par image
    size_t pos = path.find_last_of("/\\");
    string name = (pos == string::npos) ? path : path.substr(pos + 1);
    imwrite("resultat_" + name, out);

    Mat shown;
    double s2 = min(1.0, min(650.0 / out.rows, 900.0 / out.cols));
    resize(out, shown, Size(), s2, s2, INTER_AREA);
    imshow("Resultat (vue de dessus)", shown);
    waitKey(0);
    return 0;
}

int main(int argc, char** argv) {
    vector<string> images;
    if (argc > 1) {                                  // images donnees en argument
        for (int i = 1; i < argc; i++) images.push_back(argv[i]);
    } else {                                         // sinon : les 3 images de test du devoir
        images.push_back("receipt.jpg");
        images.push_back("desk.JPG");
        images.push_back("cell_pic.jpg");
    }

    for (size_t i = 0; i < images.size(); i++) {
        cout << "\n=== " << images[i] << " ===" << endl;
        if (process(images[i]) < 0) break;           // Echap : arret
        destroyAllWindows();                         // on ferme les fenetres avant l'image suivante
    }
    return 0;
}
