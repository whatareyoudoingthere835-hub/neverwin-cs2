#include "hitbox.h"

constexpr float K_EPSILON = 1e-6f;

float segment_to_segment_dist_sq(const vec3_t& s1_p0, const vec3_t& s1_p1, const vec3_t& s2_p0, const vec3_t& s2_p1) {
    vec3_t u = s1_p1 - s1_p0;
    vec3_t v = s2_p1 - s2_p0;
    vec3_t w = s1_p0 - s2_p0;

    float a = u.dot(u);
    float b = u.dot(v);
    float c = v.dot(v);
    float d = u.dot(w);
    float e = v.dot(w);

    float D = a * c - b * b;
    float sc, tc;

    if (D < FLT_EPSILON) {
        sc = 0.0f;
        tc = (b > c ? d / b : e / c);
    }
    else {
        sc = (b * e - c * d) / D;
        tc = (a * e - b * d) / D;
    }

    sc = std::max(0.0f, std::min(1.0f, sc));
    tc = std::max(0.0f, std::min(1.0f, tc));

    vec3_t dP = w + (u * sc) - (v * tc);
    return dP.dot(dP);
}

bool c_hitbox_data::segment_intersects_capsule(const vec3_t& start, const vec3_t& end) {
    if (m_radius <= 0.0f) {
        vec3_t seg_min = { std::min(start.x, end.x), std::min(start.y, end.y), std::min(start.z, end.z) };
        vec3_t seg_max = { std::max(start.x, end.x), std::max(start.y, end.y), std::max(start.z, end.z) };

        if (m_maxs.x < seg_min.x || m_mins.x > seg_max.x ||
            m_maxs.y < seg_min.y || m_mins.y > seg_max.y ||
            m_maxs.z < seg_min.z || m_mins.z > seg_max.z) {
            return false;
        }
        return true;
    }

    if ((m_maxs - m_mins).length() < 0.1f) {
        vec3_t center = (m_mins + m_maxs) * 0.5f;

        vec3_t line_dir = end - start;
        float line_len = line_dir.length();
        if (line_len < 0.001f) return center.dist_to(start) <= m_radius;

        line_dir /= line_len;
        vec3_t to_center = center - start;

        float proj = to_center.dot(line_dir);
        if (proj < 0.0f) proj = 0.0f;
        if (proj > line_len) proj = line_len;

        vec3_t closest = start + line_dir * proj;
        float dist_sq = center.dist_to_sq(closest);

        return dist_sq <= (m_radius * m_radius);
    }

    vec3_t v = m_maxs - m_mins;
    vec3_t line_dir = end - start;
    float line_len = line_dir.length();
    if (line_len < 0.001f) {
        vec3_t w = start - m_mins;
        float c1 = w.dot(v);
        if (c1 <= 0.0f) return start.dist_to_sq(m_mins) <= (m_radius * m_radius);
        float c2 = v.dot(v);
        if (c2 <= c1) return start.dist_to_sq(m_maxs) <= (m_radius * m_radius);
        float b = c1 / c2;
        vec3_t pb = m_mins + v * b;
        return start.dist_to_sq(pb) <= (m_radius * m_radius);
    }

    line_dir /= line_len;

    vec3_t u = v / v.length();
    float capsule_len = v.length();

    float denom = line_dir.dot(u);
    vec3_t w0 = start - m_mins;

    float a = line_dir.dot(line_dir) - denom * denom;
    float b = 2.0f * (line_dir.dot(w0) - denom * (w0.dot(u)));
    float c = w0.dot(w0) - m_radius * m_radius - w0.dot(u) * w0.dot(u);

    if (a < 0.001f) {
        float t = -c / b;
        if (t >= 0.0f && t <= line_len) {
            vec3_t point_on_line = start + line_dir * t;
            vec3_t point_on_capsule = m_mins + u * (w0.dot(u) + t * denom);
            point_on_capsule.x = (point_on_capsule.x < m_mins.x) ? m_mins.x : (point_on_capsule.x > m_maxs.x ? m_maxs.x : point_on_capsule.x);
            point_on_capsule.y = (point_on_capsule.y < m_mins.y) ? m_mins.y : (point_on_capsule.y > m_maxs.y ? m_maxs.y : point_on_capsule.y);
            point_on_capsule.z = (point_on_capsule.z < m_mins.z) ? m_mins.z : (point_on_capsule.z > m_maxs.z ? m_maxs.z : point_on_capsule.z);
            return point_on_line.dist_to_sq(point_on_capsule) <= (m_radius * m_radius);
        }
        return false;
    }

    float discr = b * b - 4.0f * a * c;
    if (discr < 0.0f) return false;

    float sqrt_discr = sqrtf(discr);
    float t1 = (-b - sqrt_discr) / (2.0f * a);
    float t2 = (-b + sqrt_discr) / (2.0f * a);

    if (t1 > line_len && t2 > line_len) return false;
    if (t1 < 0.0f && t2 < 0.0f) return false;

    t1 = (t1 < 0.0f) ? 0.0f : (t1 > line_len ? line_len : t1);
    t2 = (t2 < 0.0f) ? 0.0f : (t2 > line_len ? line_len : t2);

    for (float t : {t1, t2}) {
        if (t >= 0.0f && t <= line_len) {
            vec3_t point_on_line = start + line_dir * t;
            float proj_on_capsule = w0.dot(u) + t * denom;
            proj_on_capsule = (proj_on_capsule < 0.0f) ? 0.0f : (proj_on_capsule > capsule_len ? capsule_len : proj_on_capsule);
            vec3_t point_on_capsule = m_mins + u * proj_on_capsule;

            if (point_on_line.dist_to_sq(point_on_capsule) <= (m_radius * m_radius)) {
                return true;
            }
        }
    }

    return false;
}

float c_hitbox_data::projected_valid_radius(const vec3_t& view_point, const vec3_t& point_on_capsule) {

    const vec3_t vec_to_point = point_on_capsule - m_mins;
    float t = vec_to_point.dot(m_dir);
    t = std::clamp(t, 0.0f, m_axis_len);

    const vec3_t closest_point_on_axis = m_mins + m_dir * t;
    vec3_t radial = point_on_capsule - closest_point_on_axis;

    float radial_length_sqr = radial.length_sqr();
    if (radial_length_sqr < 1e-6f) {
        return 0.0f;
    }

    vec3_t view_dir = (point_on_capsule - view_point);
    float view_distance_sqr = view_dir.length_sqr();

    if (view_distance_sqr < 1e-6f) {
        return m_radius;
    }

    view_dir = view_dir.normalized();
    vec3_t radial_normalized = radial.normalized();

    vec3_t cross_product = radial_normalized.cross(view_dir);
    float sin_angle = cross_product.length();

    return std::min(m_radius * sin_angle, m_radius);
}