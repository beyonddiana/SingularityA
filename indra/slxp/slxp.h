#ifndef SLXP_H
#define SLXP_H

#include <string>
#include <vector>
#include <ostream>
#include <type_traits>
#include <sstream>
#include <array>

struct BinarySerializable {
    virtual std::ostream& serialize(std::ostream& os) const = 0;
};

struct JSONSerializable {
    virtual std::string toJSON() const = 0;
};

template <typename T>
std::ostream& unsafe_write(std::ostream& os, const T value) {
    return os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
std::ostream& unsafe_write(std::ostream& os, const T* array, const size_t len) {
    unsafe_write(os, len);
    for (size_t i = 0; i < len; i++)
        os.write(reinterpret_cast<const char*>(&array[i]), sizeof(T));
    return os;
}

template <typename T>
std::ostream& unsafe_write(std::ostream& os, const T* array[], const size_t len0, const size_t len1) {
    unsafe_write(os, len0);
    unsafe_write(os, len1);
    for (size_t i = 0; i < len0; i++)
        for (size_t j = 0; j < len1; j++)
            os.write(reinterpret_cast<const char*>(&array[i][j]), sizeof(T));
    return os;
}

std::ostream& unsafe_write(std::ostream& os, std::string value) {
    return unsafe_write(os, value.c_str(), value.size());
}

template <typename T>
std::ostream& unsafe_write(std::ostream& os, const std::vector<T>& values) {
    unsafe_write(os, values.size());
    for (auto& value : values)
        os.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return os;
}

template <typename T>
std::ostream& serialize_vector(std::ostream& os, const std::vector<T>& values) {
    //static_assert(std::is_base_of<Serializable, T>::value)
    unsafe_write(os, values.size());
    for (auto& value : values)
    {
        auto serializable = (const BinarySerializable*)&value;
        if (serializable)
            serializable->serialize(os);
    }
    return os;
}

typedef std::vector<unsigned short> Indices_list_t;
typedef std::array<std::array<float, 4>, 4> matrix_4x4_t;

std::string vectorToJSON(const Indices_list_t values) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < values.size(); i++)
    {
        os << values[i];
        if (i != (values.size() - 1))
            os << ",";
    }
    os << "]";
    return os.str();
}

inline std::ostream& arrayToJSON(std::ostream& os, const float* values, const size_t len)
{
    os << "[";
    for (size_t i = 0; i < len; i++)
    {
        os << values[i];
        if (i != (len - 1))
            os << ",";
    }
    os << "]";
    return os;
}

template <typename T, size_t len>
inline std::ostream& arrayToJSON(std::ostream& os, const std::array<T, len> values)
{
    os << "[";
    for (size_t i = 0; i < len; i++)
    {
        os << values[i];
        if (i != (len - 1))
            os << ",";
    }
    os << "]";
    return os;
}

template <typename T, size_t len>
inline std::string arrayToJSON(const std::array<T, len> values)
{
    std::ostringstream os;
    arrayToJSON(os, values);
    return os.str();
}

template <typename T, size_t len0, size_t len1>
inline std::ostream& arrayToJSON(std::ostream& os, const std::array<std::array<T, len0>, len1>& values)
{
    os << "[";
    for (size_t i = 0; i < len0; i++)
    {
        arrayToJSON(os, values[i]);
        if (i != (len0 - 1))
            os << ",";
    }
    os << "]";
    return os;
}

template <typename T, size_t len0, size_t len1>
inline std::string arrayToJSON(const std::array<std::array<T, len0>, len1>& values) {
    std::ostringstream os;
    arrayToJSON(os, values);
    return os.str();
}

std::string arrayToJSON(const float* values, const size_t len)
{
    std::ostringstream os;
    arrayToJSON(os, values, len);
    return os.str();
}

std::string arrayToJSON(const float* values, const size_t len0, const size_t len1)
{
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < len0; i++)
    {
        arrayToJSON(os, &values[len1 * i], len1);
        if (i != (len0 - 1))
            os << ",";
    }
    os << "]";
    return os.str();
}

template <typename T>
std::string vectorToJSON(std::vector<T> values) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < values.size(); i++)
    {
        auto& value = values[i];
        auto serializable = (const JSONSerializable*)(&value);
        if (serializable)
            os << serializable->toJSON();
        if (i != (values.size() - 1))
            os << ",";
    }
    os << "]";
    return os.str();
}

// This is probably really slow and I don't caaaare
inline float sanitize(float x)
{
    if (!std::isfinite(x))
        if (std::isnan(x))
            return 0.f;
        else if (x > 0)
            return 10e30f;
        else
            return -10e30f;
    return x;
}

struct Vec2 : public BinarySerializable, public JSONSerializable {
    float x;
    float y;
    inline virtual std::ostream& serialize(std::ostream& os) const override
    {
        unsafe_write(os, x);
        unsafe_write(os, y);
        return os;
    }
    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "["
            << sanitize(x) << ","
            << sanitize(y) << "]";
        return os.str();
    }
};

struct Vec3 : public Vec2 {
    float z;
    inline virtual std::ostream& serialize(std::ostream& os) const override
    {
        Vec2::serialize(os);
        unsafe_write(os, z);
        return os;
    }
    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "["
            << sanitize(x) << ","
            << sanitize(y) << ","
            << sanitize(z) << "]";
        return os.str();
    }
};

struct Vec4 : public Vec3 {
    float w;
    inline virtual std::ostream& serialize(std::ostream& os) const override
    {
        Vec3::serialize(os);
        unsafe_write(os, w);
        return os;
    }
    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "["
            << sanitize(x) << ","
            << sanitize(y) << ","
            << sanitize(z) << ","
            << sanitize(w) << "]";
        return os.str();
    }
};

typedef std::vector<Vec2> Vec2_list_t;
typedef std::vector<Vec3> Vec3_list_t;
typedef std::vector<Vec4> Vec4_list_t;

template <typename T>
static constexpr T* begin(T& value) noexcept
{
    return &value;
}

template <typename T, ::std::size_t N>
static constexpr typename ::std::remove_all_extents<T>::type*
begin(T(&array)[N]) noexcept
{
    return begin(*array);
}

template <typename T, ::std::size_t N>
static constexpr typename ::std::remove_all_extents<T>::type*
begin(std::array<T, N>& array_) noexcept
{
    return &array_[0];
}

template <typename T, ::std::size_t N, ::std::size_t M>
static constexpr typename ::std::remove_all_extents<T>::type*
begin(std::array<std::array<T, M>, N>& array_) noexcept
{
    return begin(array_[0]);
}


template <typename T>
static constexpr T* end(T& value) noexcept
{
    return &value + 1;
}

template <typename T, ::std::size_t N>
static constexpr typename ::std::remove_all_extents<T>::type*
end(T(&array)[N]) noexcept
{
    return end(array[N - 1]);
}

template <typename T, ::std::size_t N>
std::array<T, N> toArray(const T(&array)[N]) noexcept
{
    std::array<T, N> rval;
    std::copy(begin(array), end(array), begin(rval));
    return rval;
}

template <typename T, ::std::size_t N, ::std::size_t M>
std::array<std::array<T, M>, N> toArray(const T(&array)[N][M]) noexcept
{
    std::array<std::array<T, M>, N> rval;
    std::copy(begin(array), end(array), begin(rval));
    return rval;
}

struct WithTRS : public BinarySerializable, public JSONSerializable {
    Vec3 LocalPosition;
    Vec4 LocalRotation;
    Vec3 LocalScale;
    virtual std::ostream& serialize(std::ostream& os) const override
    {
        LocalPosition.serialize(os);
        LocalRotation.serialize(os);
        LocalScale.serialize(os);
        return os;
    }
    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "\"LocalPosition\": " << LocalPosition.toJSON() << "," << std::endl
            << "\"LocalRotation\": " << LocalRotation.toJSON() << "," << std::endl
            << "\"LocalScale\": " << LocalScale.toJSON();
        return os.str();
    }
};

struct WithName {
    std::string Name;
    WithName(std::string name) :
        Name(name)
    { }
};

struct WithId {
    unsigned int Id;
    WithId(unsigned int id) :
        Id(id)
    { }
};

struct WithParentId {
    unsigned int ParentId;
    WithParentId(unsigned int parent_id) :
        ParentId(parent_id)
    { }
};

// -------- //

struct SLXPFace : public BinarySerializable, public JSONSerializable {
    Vec3_list_t Positions;
    Vec3_list_t Normals;
    Vec3_list_t Tangents;
    Vec2_list_t TexCoords;
    Vec2 TexCoordsOffset;
    Vec2 TexCoordsScale;
    float TexCoordsRotation;
    Vec4_list_t Weights;
    Indices_list_t Indices;

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        serialize_vector(os, Positions);
        serialize_vector(os, Normals);
        serialize_vector(os, Tangents);
        serialize_vector(os, TexCoords);
        unsafe_write(os, TexCoordsOffset);
        unsafe_write(os, TexCoordsScale);
        unsafe_write(os, TexCoordsRotation);
        serialize_vector(os, Weights);
        serialize_vector(os, Indices);
        return os;
    }

    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "{" << std::endl
            << "\"Positions\": " << vectorToJSON(Positions) << "," << std::endl
            << "\"Normals\": " << vectorToJSON(Normals) << "," << std::endl
            << "\"Tangents\": " << vectorToJSON(Tangents) << "," << std::endl
            << "\"TexCoords\": " << vectorToJSON(TexCoords) << "," << std::endl
            << "\"TexCoordOffset\": " << TexCoordsOffset.toJSON() << "," << std::endl
            << "\"TexCoordScale\": " << TexCoordsScale.toJSON() << "," << std::endl
            << "\"TexCoordsRotation\": " << TexCoordsRotation << "," << std::endl
            << "\"Weights\": " << vectorToJSON(Weights) << "," << std::endl
            << "\"Indices\": " << vectorToJSON(Indices) << std::endl
            << "}";
        return os.str();
    }
};

struct SLXPObjectBaseMixin :
    public WithName,
    public WithId,
    public WithParentId,
    public WithTRS
{
    SLXPObjectBaseMixin(std::string name, unsigned int id, unsigned int parent_id = 0) :
        WithName(name),
        WithId(id),
        WithParentId(parent_id)
    { }

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        unsafe_write(os, Name);
        WithTRS::serialize(os);
        return os;
    }

    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "\"Name\": \"" << Name << "\"," << std::endl
            << "\"Id\": " << Id << "," << std::endl
            << "\"ParentId\": " << ParentId << "," << std::endl
            << WithTRS::toJSON();
        return os.str();
    }
};

struct SLXPObject : public SLXPObjectBaseMixin
{
private:
    bool _HasBindShapeMatrix;
public:
    std::vector<SLXPFace> Faces;
    matrix_4x4_t BindShapeMatrix;
    std::vector<matrix_4x4_t> InverseBindMatrices;
    std::vector<int> JointNumbers;
    int AttachmentJointId;
    int LinkNumber;
    SLXPObject(std::string name, unsigned int id, unsigned int parent_id = 0) :
        SLXPObjectBaseMixin(name, id, parent_id),
        _HasBindShapeMatrix(false),
        BindShapeMatrix(matrix_4x4_t()),
        AttachmentJointId(0),
        LinkNumber(0)
    {
    }

    void setBindShapeMatrix(const std::array<std::array<float, 4>, 4> mtx) {
        BindShapeMatrix = mtx;
        _HasBindShapeMatrix = true;
    }

    void setBindShapeMatrix(const float(&mtx)[4][4]) {
        BindShapeMatrix = toArray(mtx);
        _HasBindShapeMatrix = true;
    }

    void unsetBindShapeMatrix()
    {
        //auto dst = &BindShapeMatrix[0][0];
        //std::fill(dst, dst + 16, 0.f);
        _HasBindShapeMatrix = false;
    }

    bool hasBindShapeMatrix() const
    {
        return _HasBindShapeMatrix;
    }

    void clearInverseBindMatrices()
    {
        InverseBindMatrices.clear();
    }

    void addInverseBindMatrix(const std::array<std::array<float, 4>, 4> mtx)
    {
        InverseBindMatrices.push_back(mtx);
    }

    void addInverseBindMatrix(const float(&mtx)[4][4])
    {
        InverseBindMatrices.push_back(toArray(mtx));
    }

    void addJointNumber(const int joint_number)
    {
        JointNumbers.push_back(joint_number);
    }

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        SLXPObjectBaseMixin::serialize(os);
        unsafe_write(os, Faces);
        return os;
    }

    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "{" << std::endl
            << SLXPObjectBaseMixin::toJSON() << "," << std::endl;

        if (JointNumbers.size() > 0)
        {
            os << "\"JointNumbers\": [";
            const size_t size = JointNumbers.size();
            for (size_t i = 0; i < size; i++)
            {
                const auto& joint_number = JointNumbers[i];
                os << joint_number;
                if (i != (size - 1))
                    os << ",";
            }
            os << "]," << std::endl;
        }

        if (_HasBindShapeMatrix)
            os << "\"BindShapeMatrix\": " << arrayToJSON(BindShapeMatrix) << "," << std::endl;

        if (InverseBindMatrices.size() > 0)
        {
            os << "\"InverseBindMatrices\": [";
            const size_t size = InverseBindMatrices.size();
            for (size_t i = 0; i < size; i++)
            {
                const auto& inv_bind_mtx = InverseBindMatrices[i];
                arrayToJSON(os, inv_bind_mtx);
                if (i != (size - 1))
                    os << "," << std::endl;
            }
            os << "]," << std::endl;
        }

        os << "\"AttachmentJointId\": " << AttachmentJointId << "," << std::endl;
        os << "\"LinkNumber\": " << LinkNumber << "," << std::endl;
        os << "\"Faces\": " << vectorToJSON(Faces) << std::endl
            << "}";
        return os.str();
    }
};

struct SLXPJoint : public SLXPObjectBaseMixin
{

    SLXPJoint(std::string name, unsigned int id, unsigned int parent_id = 0) :
        SLXPObjectBaseMixin(name, id, parent_id)
    {
    }

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        SLXPObjectBaseMixin::serialize(os);
        return os;
    }

    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "{" << std::endl
            << SLXPObjectBaseMixin::toJSON() << std::endl
            << "}";
        return os.str();
    }
};

struct SLXPCollection : public BinarySerializable, public JSONSerializable {
    std::vector<SLXPObject> Objects;
    std::vector<SLXPJoint> Joints;

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        serialize_vector(os, Objects);
        return os;
    }
    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "{" << std::endl
            << "\"Objects\": " << vectorToJSON(Objects) << "," << std::endl
            << "\"Joints\": " << vectorToJSON(Joints) << std::endl
            << "}";
        return os.str();
    }
};

struct SLXP : public BinarySerializable, public JSONSerializable {
    const unsigned char FormatVersionA = 0;
    const unsigned char FormatVersionB = 0;
    const unsigned char FormatVersionC = 1;
    std::string Title;
    SLXPCollection Collection;

    SLXP(std::string title) :
        Title(title)
    {
    }

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        unsafe_write(os, 'S');
        unsafe_write(os, 'L');
        unsafe_write(os, 'X');
        unsafe_write(os, 'P');
        unsafe_write(os, FormatVersionA);
        unsafe_write(os, FormatVersionB);
        unsafe_write(os, FormatVersionC);
        unsafe_write(os, Title);
        Collection.serialize(os);
        return os;
    }

    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "{" << std::endl
            << "\"Title\": \"" << Title << "\", " << std::endl
            << "\"Collection\": " << Collection.toJSON() << std::endl
            << "}";
        return os.str();
    }
};

#endif