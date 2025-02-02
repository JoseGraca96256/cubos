#ifndef CUBOS_CORE_ECS_WORLD_VIEW_HPP
#define CUBOS_CORE_ECS_WORLD_VIEW_HPP

#include <cubos/core/ecs/world.hpp>

namespace cubos::core::ecs
{

    /// @brief WorldView is an iterator over the entities of a world
    /// that contain a certain set of components.
    /// @tparam ComponentTypes The set of component types to be iterated.
    template <typename... ComponentTypes> struct WorldView
    {
        World* world;
        std::vector<uint8_t> mask;

        WorldView(World& w);

        struct Iterator
        {
            World* world;
            size_t current;
            std::vector<uint8_t>* mask;

            Iterator(World* w, size_t index, std::vector<uint8_t>* m);

            size_t operator*() const;

            bool operator==(const Iterator& other) const;

            bool operator!=(const Iterator& other) const;

            bool isValidID();

            Iterator& operator++();
        };

        bool isValidIndex(size_t index);

        Iterator begin();

        Iterator end();
    };

    template <typename... ComponentTypes> WorldView<ComponentTypes...>::WorldView(World& w) : world(&w)
    {
        mask.resize(world->elementsPerEntity - 1);
        size_t componentIds[] = {world->getComponentID<ComponentTypes>()...};
        for (auto id : componentIds)
        {
            mask[id / 32] |= 1 << (id % 32);
        }
    }

    template <typename... ComponentTypes>
    WorldView<ComponentTypes...>::Iterator::Iterator(World* w, size_t index, std::vector<uint8_t>* m)
        : world(w), current(index), mask(m)
    {
    }

    template <typename... ComponentTypes> size_t WorldView<ComponentTypes...>::Iterator::operator*() const
    {
        return current;
    }

    template <typename... ComponentTypes>
    bool WorldView<ComponentTypes...>::Iterator::operator==(const Iterator& other) const
    {
        return current == other.current || current == world->nextEntityId;
    }

    template <typename... ComponentTypes>
    bool WorldView<ComponentTypes...>::Iterator::operator!=(const Iterator& other) const
    {
        return current != other.current && current != world->nextEntityId;
    }

    template <typename... ComponentTypes> bool WorldView<ComponentTypes...>::Iterator::isValidID()
    {
        for (size_t i = 0; i < mask->size(); i++)
        {
            if ((mask->at(i) & world->entityData[current * world->elementsPerEntity + 1 + i]) != mask->at(i))
                return false;
        }
        return true;
    }

    template <typename... ComponentTypes>
    typename WorldView<ComponentTypes...>::Iterator& WorldView<ComponentTypes...>::Iterator::operator++()
    {
        do
        {
            current++;
        } while (current < world->nextEntityId && !isValidID());
        return *this;
    }

    template <typename... ComponentTypes> bool WorldView<ComponentTypes...>::isValidIndex(size_t index)
    {
        for (size_t i = 0; i < mask.size(); i++)
        {
            if ((mask[i] & world->entityData[index * world->elementsPerEntity + 1 + i]) != mask[i])
                return false;
        }

        return true;
    }

    template <typename... ComponentTypes>
    typename WorldView<ComponentTypes...>::Iterator WorldView<ComponentTypes...>::begin()
    {
        size_t firstIndex = 0;
        while (firstIndex < world->nextEntityId && !isValidIndex(firstIndex))
        {
            firstIndex++;
        }

        Iterator it(world, firstIndex, &mask);
        return it;
    }

    template <typename... ComponentTypes>
    typename WorldView<ComponentTypes...>::Iterator WorldView<ComponentTypes...>::end()
    {
        Iterator it(world, world->nextEntityId, &mask);
        return it;
    }

} // namespace cubos::core::ecs

#endif // CUBOS_CORE_ECS_WORLD_VIEW_HPP
